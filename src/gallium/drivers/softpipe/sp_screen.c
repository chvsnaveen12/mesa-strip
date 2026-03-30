/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "compiler/nir/nir.h"
#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_screen.h"
#include "util/u_video.h"
#include "util/os_misc.h"
#include "util/os_time.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"

#include "frontend/sw_winsys.h"
#include "tgsi/tgsi_exec.h"

#include "sp_texture.h"
#include "sp_screen.h"
#include "sp_context.h"
#include "sp_fence.h"
#include "sp_public.h"

static const struct debug_named_value sp_debug_options[] = {
   {"vs",        SP_DBG_VS,         "dump vertex shader assembly to stderr"},
   {"gs",        SP_DBG_GS,         "dump geometry shader assembly to stderr"},
   {"fs",        SP_DBG_FS,         "dump fragment shader assembly to stderr"},
   {"cs",        SP_DBG_CS,         "dump compute shader assembly to stderr"},
   {"no_rast",   SP_DBG_NO_RAST,    "no-ops rasterization, for profiling purposes"},
   {"use_llvm",  SP_DBG_USE_LLVM,   "Use LLVM if available for shaders"},
   DEBUG_NAMED_VALUE_END
};

int sp_debug;
DEBUG_GET_ONCE_FLAGS_OPTION(sp_debug, "SOFTPIPE_DEBUG", sp_debug_options, 0)

static const char *
softpipe_get_vendor(struct pipe_screen *screen)
{
   return "Mesa";
}


static const char *
softpipe_get_name(struct pipe_screen *screen)
{
   return "softpipe";
}

static const nir_shader_compiler_options sp_compiler_options = {
   .fdot_replicates = true,
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_fdph = true,
   .lower_flrp64 = true,
   .lower_fmod = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .lower_int64_options = nir_lower_imul_2x32_64,
   .max_unroll_iterations = 32,

   /* TGSI doesn't have a semantic for local or global index, just local and
    * workgroup id.
    */
   .lower_cs_local_index_to_id = true,
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(MESA_SHADER_STAGES),
};

/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static bool
softpipe_is_format_supported( struct pipe_screen *screen,
                              enum pipe_format format,
                              enum pipe_texture_target target,
                              unsigned sample_count,
                              unsigned storage_sample_count,
                              unsigned bind)
{
   struct sw_winsys *winsys = softpipe_screen(screen)->winsys;
   const struct util_format_description *format_desc;

   assert(target == PIPE_BUFFER ||
          target == PIPE_TEXTURE_1D ||
          target == PIPE_TEXTURE_1D_ARRAY ||
          target == PIPE_TEXTURE_2D ||
          target == PIPE_TEXTURE_2D_ARRAY ||
          target == PIPE_TEXTURE_RECT ||
          target == PIPE_TEXTURE_3D ||
          target == PIPE_TEXTURE_CUBE ||
          target == PIPE_TEXTURE_CUBE_ARRAY);

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   format_desc = util_format_description(format);

   if (sample_count > 1)
      return false;

   if (bind & (PIPE_BIND_DISPLAY_TARGET |
               PIPE_BIND_SCANOUT |
               PIPE_BIND_SHARED)) {
      if(!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return false;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return false;

      /*
       * Although possible, it is unnatural to render into compressed or YUV
       * surfaces. So disable these here to avoid going into weird paths
       * inside gallium frontends.
       */
      if (format_desc->block.width != 1 ||
          format_desc->block.height != 1)
         return false;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_ATC) {
      /* Software decoding is not hooked up. */
      return false;
   }

   if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
       ((bind & PIPE_BIND_DISPLAY_TARGET) == 0) &&
       target != PIPE_BUFFER) {
      const struct util_format_description *desc =
         util_format_description(format);
      if (desc->nr_channels == 3 && desc->is_array) {
         /* Don't support any 3-component formats for rendering/texturing
          * since we don't support the corresponding 8-bit 3 channel UNORM
          * formats.  This allows us to support GL_ARB_copy_image between
          * GL_RGB8 and GL_RGB8UI, for example.  Otherwise, we may be asked to
          * do a resource copy between PIPE_FORMAT_R8G8B8_UINT and
          * PIPE_FORMAT_R8G8B8X8_UNORM, for example, which will not work
          * (different bpp).
          */
         return false;
      }
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8)
      return false;

   /*
    * All other operations (sampling, transfer, etc).
    */

   /*
    * Everything else should be supported by u_format.
    */
   return true;
}


static void
softpipe_init_shader_caps(struct softpipe_screen *sp_screen)
{
   /* TEMP: use mypipe's ES 2.0 level shader caps for testing */
   static const unsigned stages[] = { MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT };
   for (unsigned s = 0; s < 2; s++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&sp_screen->base.shader_caps[stages[s]];

      caps->max_instructions =
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections = 256;
      caps->max_outputs = 8;
      caps->max_control_flow_depth = 8;
      caps->max_inputs = 8;
      caps->max_const_buffer0_size = (256 * sizeof(float[4]));
      caps->max_const_buffers = 1;
      caps->max_temps = 32;
      caps->indirect_const_addr = false;
      caps->subroutines = false;
      caps->integers = true;
      caps->max_texture_samplers = 8;
      caps->max_sampler_views = 8;
      caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
      caps->max_shader_buffers = 0;
      caps->max_shader_images = 0;
   }
}


/* stripped: no compute caps */


static void
softpipe_init_screen_caps(struct softpipe_screen *sp_screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&sp_screen->base.caps;

   /* TEMP: use mypipe's ES 2.0 level screen caps for testing */
   u_init_pipe_screen_caps(&sp_screen->base, 0);

   caps->alpha_test = true;
   caps->flatshade = true;
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->fragment_shader_texture_lod = false;
   caps->fragment_shader_derivatives = true;
   caps->anisotropic_filter = false;
   caps->max_render_targets = 1;
   caps->max_dual_source_render_targets = 0;
   caps->occlusion_query = false;
   caps->query_time_elapsed = false;
   caps->query_pipeline_statistics = false;
   caps->texture_mirror_clamp = false;
   caps->texture_mirror_clamp_to_edge = false;
   caps->texture_swizzle = true;
   caps->max_texture_2d_size = 2048;
   caps->max_texture_3d_levels = 0;
   caps->max_texture_cube_levels = 12;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = false;
   caps->indep_blend_func = false;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_origin_lower_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->depth_clip_disable = false;
   caps->depth_bounds_test = false;
   caps->max_stream_output_buffers = 0;
   caps->max_stream_output_separate_components = 0;
   caps->max_stream_output_interleaved_components = 0;
   caps->max_geometry_output_vertices = 0;
   caps->max_geometry_total_output_components = 0;
   caps->max_vertex_streams = 0;
   caps->max_vertex_attrib_stride = 2048;
   caps->primitive_restart = false;
   caps->supported_prim_modes_with_restart =
   caps->supported_prim_modes = (BITFIELD_MASK(MESA_PRIM_COUNT) & ~BITFIELD_BIT(MESA_PRIM_QUADS) & ~BITFIELD_BIT(MESA_PRIM_QUAD_STRIP));
   caps->primitive_restart_fixed_index = false;
   caps->shader_stencil_export = false;
   caps->vs_instanceid = false;
   caps->vertex_element_instance_divisor = false;
   caps->start_instance = false;
   caps->seamless_cube_map = false;
   caps->seamless_cube_map_per_texture = false;
   caps->max_texture_array_layers = 0;
   caps->min_texel_offset = 0;
   caps->max_texel_offset = 0;
   caps->conditional_render = false;
   caps->fragment_color_clamped = true;
   caps->vertex_color_unclamped = true;
   caps->vertex_color_clamped = true;
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 120;
   caps->compute = false;
   caps->user_vertex_buffers = true;
   caps->stream_output_pause_resume = false;
   caps->stream_output_interleave_buffers = false;
   caps->vs_layer_viewport = false;
   caps->doubles = false;
   caps->int64 = false;
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = 64;
   caps->query_timestamp = false;
   caps->timer_resolution = false;
   caps->cube_map_array = false;
   caps->texture_buffer_objects = false;
   caps->max_texel_buffer_elements = 0;
   caps->texture_buffer_offset_alignment = 0;
   caps->texture_transfer_modes = 0;
   caps->max_viewports = 1;
   caps->endianness = 0;
   caps->max_texture_gather_components = 0;
   caps->texture_gather_sm5 = false;
   caps->texture_query_lod = false;
   caps->vs_window_space_position = false;
   caps->fs_fine_derivative = false;
   caps->sampler_view_target = false;
   caps->fake_sw_msaa = false;
   caps->min_texture_gather_offset = 0;
   caps->max_texture_gather_offset = 0;
   caps->draw_indirect = false;
   caps->query_so_overflow = false;
   caps->nir_images_as_deref = false;
   caps->shareable_shaders = false;

   caps->vendor_id = 0xFFFFFFFF;
   caps->device_id = 0xFFFFFFFF;

   uint64_t system_memory;
   if (os_get_total_physical_memory(&system_memory)) {
      caps->video_memory = system_memory >> 20;
   } else {
      caps->video_memory = 0;
   }

   caps->uma = true;
   caps->query_memory_info = false;
   caps->conditional_render_inverted = false;
   caps->clip_halfz = false;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->framebuffer_no_attachment = false;
   caps->cull_distance = false;
   caps->copy_between_compressed_and_plain_formats = false;
   caps->shader_array_components = false;
   caps->tgsi_texcoord = true;
   caps->max_varyings = 8;
   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->max_gs_invocations = 0;
   caps->max_shader_buffer_size = 0;
   caps->shader_buffer_offset_alignment = 0;
   caps->image_store_formatted = false;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 255.0;
   caps->max_point_size =
   caps->max_point_size_aa = 255.0;
   caps->max_texture_anisotropy = 16.0;
   caps->max_texture_lod_bias = 16.0;
}


static void
softpipe_destroy_screen( struct pipe_screen *screen )
{
   FREE(screen);
}


/* This is often overriden by the co-state tracker.
 */
static void
softpipe_flush_frontbuffer(struct pipe_screen *_screen,
                           struct pipe_context *pipe,
                           struct pipe_resource *resource,
                           unsigned level, unsigned layer,
                           void *context_private,
                           unsigned nboxes,
                           struct pipe_box *sub_box)
{
   struct softpipe_screen *screen = softpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct softpipe_resource *texture = softpipe_resource(resource);

   assert(texture->dt);
   if (texture->dt)
      winsys->displaytarget_display(winsys, texture->dt, context_private, nboxes, sub_box);
}

static int
softpipe_screen_get_fd(struct pipe_screen *screen)
{
   struct sw_winsys *winsys = softpipe_screen(screen)->winsys;

   if (winsys->get_fd)
      return winsys->get_fd(winsys);
   else
      return -1;
}

/**
 * Create a new pipe_screen object
 * Note: we're not presently subclassing pipe_screen (no softpipe_screen).
 */
struct pipe_screen *
softpipe_create_screen(struct sw_winsys *winsys)
{
   struct softpipe_screen *screen = CALLOC_STRUCT(softpipe_screen);

   if (!screen)
      return NULL;

   sp_debug = debug_get_option_sp_debug();

   screen->winsys = winsys;

   screen->base.destroy = softpipe_destroy_screen;

   screen->base.get_name = softpipe_get_name;
   screen->base.get_vendor = softpipe_get_vendor;
   screen->base.get_device_vendor = softpipe_get_vendor; // TODO should be the CPU vendor
   screen->base.get_screen_fd = softpipe_screen_get_fd;
   screen->base.get_timestamp = u_default_get_timestamp;
   screen->base.query_memory_info = util_sw_query_memory_info;
   screen->base.is_format_supported = softpipe_is_format_supported;
   screen->base.context_create = softpipe_create_context;
   screen->base.flush_frontbuffer = softpipe_flush_frontbuffer;
   screen->use_llvm = sp_debug & SP_DBG_USE_LLVM;

   /* stripped: only vertex + fragment NIR options */
   screen->base.nir_options[MESA_SHADER_VERTEX] = &sp_compiler_options;
   screen->base.nir_options[MESA_SHADER_FRAGMENT] = &sp_compiler_options;

   softpipe_init_screen_texture_funcs(&screen->base);
   softpipe_init_screen_fence_funcs(&screen->base);

   softpipe_init_shader_caps(screen);
   softpipe_init_screen_caps(screen);

   return &screen->base;
}
