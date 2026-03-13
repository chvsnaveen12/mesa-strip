#include <stdio.h>
#include <stdbool.h>
#include "compiler/nir/nir.h"
#include "frontend/sw_winsys.h"
#include "util/u_memory.h"
#include "util/u_screen.h"

#include "mp_public.h"
#include "mp_screen.h"
#include "mp_context.h"
#include "mp_texture.h"
#include "mp_fence.h"

static void mypipe_destroy_screen(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_destroy_screen\n");
    FREE(screen);
}

static const char * mypipe_get_name(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_get_name\n");
    return "mypipe";
}

static const char * mypipe_get_vendor(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_get_vendor\n");
    return "Naveen";
}

static const char * mypipe_get_device_vendor(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_get_device_vendor\n");
    return "Naveen";
}

static int mypipe_get_fd(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_get_fd\n");

    struct sw_winsys *winsys = mypipe_screen(screen)->winsys;
    if(winsys->get_fd)
        return winsys->get_fd(winsys);
    else
        return -1;
}

static uint64_t mypipe_get_timestamp(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_get_timestamp\n");
    return os_time_get_nano();
}

static void mypipe_sw_query_memory_info(struct pipe_screen *screen, struct pipe_memory_info *info){
    fprintf(stderr, "STUB: mypipe_sw_query_memory_info\n");
    return;
}

static bool mypipe_is_format_supported(struct pipe_screen *screen,
                                       enum pipe_format format,
                                       enum pipe_texture_target target,
                                       unsigned int sample_count,
                                       unsigned int storage_sample_count,
                                       unsigned int bind){
    if(sample_count > 1)
        return false;

    if(target != PIPE_BUFFER && target != PIPE_TEXTURE_2D && target != PIPE_TEXTURE_CUBE)
        return false;

    if(bind & PIPE_BIND_RENDER_TARGET){
        switch (format){
            case PIPE_FORMAT_B8G8R8A8_UNORM:
            case PIPE_FORMAT_B8G8R8X8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
            case PIPE_FORMAT_R8G8B8X8_UNORM:
                return true;
            default:
                return false;
        }
    }
    if(bind & PIPE_BIND_DEPTH_STENCIL){
        switch(format){
            case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                return true;
            default:
                return false;
        }
    }
    if(bind & PIPE_BIND_SAMPLER_VIEW){
        switch (format){
            case PIPE_FORMAT_B8G8R8A8_UNORM:
            case PIPE_FORMAT_B8G8R8X8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
            case PIPE_FORMAT_R8G8B8X8_UNORM:
                return true;
            default:
                return false;
        }
    }
    if(bind & PIPE_BIND_VERTEX_BUFFER)
        return true;
    return false;
//    struct sw_winsys *winsys = mypipe_screen(screen)->winsys;
//    const struct util_format_description *format_desc;

//    assert(target == PIPE_BUFFER ||
//           target == PIPE_TEXTURE_1D ||
//           target == PIPE_TEXTURE_1D_ARRAY ||
//           target == PIPE_TEXTURE_2D ||
//           target == PIPE_TEXTURE_2D_ARRAY ||
//           target == PIPE_TEXTURE_RECT ||
//           target == PIPE_TEXTURE_3D ||
//           target == PIPE_TEXTURE_CUBE ||
//           target == PIPE_TEXTURE_CUBE_ARRAY);

//    if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
//       return false;

//    format_desc = util_format_description(format);

//    if (sample_count > 1)
//       return false;

//    if (bind & (PIPE_BIND_DISPLAY_TARGET |
//                PIPE_BIND_SCANOUT |
//                PIPE_BIND_SHARED)) {
//       if(!winsys->is_displaytarget_format_supported(winsys, bind, format))
//          return false;
//    }

//    if (bind & PIPE_BIND_RENDER_TARGET) {
//       if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
//          return false;

//       /*
//        * Although possible, it is unnatural to render into compressed or YUV
//        * surfaces. So disable these here to avoid going into weird paths
//        * inside gallium frontends.
//        */
//       if (format_desc->block.width != 1 ||
//           format_desc->block.height != 1)
//          return false;
//    }

//    if (bind & PIPE_BIND_DEPTH_STENCIL) {
//       if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
//          return false;
//    }

//    if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
//        format_desc->layout == UTIL_FORMAT_LAYOUT_ATC) {
//       /* Software decoding is not hooked up. */
//       return false;
//    }

//    if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
//        ((bind & PIPE_BIND_DISPLAY_TARGET) == 0) &&
//        target != PIPE_BUFFER) {
//       const struct util_format_description *desc =
//          util_format_description(format);
//       if (desc->nr_channels == 3 && desc->is_array) {
//          /* Don't support any 3-component formats for rendering/texturing
//           * since we don't support the corresponding 8-bit 3 channel UNORM
//           * formats.  This allows us to support GL_ARB_copy_image between
//           * GL_RGB8 and GL_RGB8UI, for example.  Otherwise, we may be asked to
//           * do a resource copy between PIPE_FORMAT_R8G8B8_UINT and
//           * PIPE_FORMAT_R8G8B8X8_UNORM, for example, which will not work
//           * (different bpp).
//           */
//          return false;
//       }
//    }

//    if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
//        format != PIPE_FORMAT_ETC1_RGB8)
//       return false;

   /*
    * All other operations (sampling, transfer, etc).
    */

   /*
    * Everything else should be supported by u_format.
    */
//    return true;

}

static void mypipe_flush_frontbuffer(struct pipe_screen * _screen,
                                     struct pipe_context *pipe,
                                     struct pipe_resource *resource,
                                     unsigned int level, unsigned int layer,
                                     void *context_private,
                                     unsigned nboxes, struct pipe_box *sub_box){
    fprintf(stderr, "STUB: mypipe_flush_frontbuffer: resource=%p format=%d %ux%u level=%u layer=%u\n",
            (void*)resource, resource->format, resource->width0, resource->height0, level, layer);
    struct mypipe_screen *screen = mypipe_screen(_screen);
    struct sw_winsys *winsys = screen->winsys;
    struct mypipe_resource *texture = mypipe_resource(resource);
    assert(texture->dt);

    if(texture->dt)
        winsys->displaytarget_display(winsys, texture->dt, context_private, nboxes, sub_box);
}

static const nir_shader_compiler_options mp_compiler_options = {
0
};

static void mypipe_init_shader_caps(struct mypipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_init_shader_caps\n");
    static const unsigned stages[] = {MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT};

    for(int i = 0; i < 2; i++){
        struct pipe_shader_caps *caps = &screen->base.shader_caps[stages[i]];

        caps->max_instructions =
        caps->max_alu_instructions =
        caps->max_tex_instructions =
        caps->max_tex_indirections = 4096;
        caps->max_outputs = 32;
        caps->max_control_flow_depth = 8;
        caps->max_inputs = 8;
        caps->max_const_buffer0_size = (4096 * sizeof(float[4]));
        caps->max_const_buffers = 1;
        caps->max_temps = 256;
        caps->indirect_const_addr = true;
        caps->subroutines = true;
        caps->integers = true;
        caps->max_texture_samplers = 8;
        caps->max_sampler_views = 8;
        caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
        caps->max_shader_buffers = 0;  /* stripped: no SSBOs/atomics */
        caps->max_shader_images = 0;   /* stripped: no image load/store */
    }
}

static void mypipe_init_screen_caps(struct mypipe_screen *mp_screen){
    fprintf(stderr, "STUB: mypipe_init_screen_caps\n");
    struct pipe_caps *caps = &mp_screen->base.caps;

    u_init_pipe_screen_caps(&mp_screen->base, 0);
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->anisotropic_filter = true;
   caps->max_render_targets = 8;
   caps->max_dual_source_render_targets = 1;
   caps->occlusion_query = true;
   caps->query_time_elapsed = true;
   caps->query_pipeline_statistics = true;
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->texture_swizzle = true;
   caps->max_texture_2d_size = 1 << (15 - 1);
   caps->max_texture_3d_levels = 12;
   caps->max_texture_cube_levels = 13;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_origin_lower_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->depth_clip_disable = true;
   caps->depth_bounds_test = true;
   caps->max_stream_output_buffers = 4;
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 16*4;
   caps->max_geometry_output_vertices = 0;  /* stripped: no geometry shaders */
   caps->max_geometry_total_output_components = 0;
   caps->max_vertex_streams = 1;
   caps->max_vertex_attrib_stride = 2048;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->shader_stencil_export = true;
   caps->image_atomic_float_add = false;  /* stripped: no atomics */
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->start_instance = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->max_texture_array_layers = 256; /* for GL3 */
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->conditional_render = true;
   caps->fragment_color_clamped = true;
   caps->vertex_color_unclamped = true; /* draw module */
   caps->vertex_color_clamped = true; /* draw module */
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 330;  /* stripped: cap at GL 3.3 */
   caps->compute = false;  /* stripped: no compute shaders */
   caps->user_vertex_buffers = true;
   caps->stream_output_pause_resume = false;
   caps->stream_output_interleave_buffers = false;
   caps->vs_layer_viewport = false;
   caps->doubles = false;  /* stripped: no fp64 */
   caps->int64 = false;    /* stripped: no int64 */
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = 64;
   caps->query_timestamp = true;
   caps->timer_resolution = true;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->max_texel_buffer_elements = 65536;
   caps->texture_buffer_offset_alignment = 16;
   caps->texture_transfer_modes = 0;
   caps->max_viewports = 16;
   caps->endianness = 0;
   caps->max_texture_gather_components = 4;
   caps->texture_gather_sm5 = true;
   caps->texture_query_lod = true;
   caps->vs_window_space_position = true;
   caps->fs_fine_derivative = true;
   caps->sampler_view_target = true;
   caps->fake_sw_msaa = true;
   caps->min_texture_gather_offset = -32;
   caps->max_texture_gather_offset = 31;
   caps->draw_indirect = false;  /* stripped: no indirect draw */
   caps->query_so_overflow = true;
   caps->nir_images_as_deref = false;

   /* Can't expose shareable shaders because the draw shaders reference the
    * draw module's state, which is per-context.
    */
   caps->shareable_shaders = false;

   caps->vendor_id = 0xFFFFFFFF;
   caps->device_id = 0xFFFFFFFF;

   /* XXX: Do we want to return the full amount fo system memory ? */
   uint64_t system_memory;
   if (os_get_total_physical_memory(&system_memory)) {
      if (sizeof(void *) == 4)
         /* Cap to 2 GB on 32 bits system. We do this because llvmpipe does
          * eat application memory, which is quite limited on 32 bits. App
          * shouldn't expect too much available memory. */
         system_memory = MIN2(system_memory, 2048 << 20);

      caps->video_memory = system_memory >> 20;
   } else {
      caps->video_memory = 0;
   }

   caps->uma = false;
   caps->query_memory_info = true;
   caps->conditional_render_inverted = true;
   caps->clip_halfz = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->shader_array_components = true;
   caps->tgsi_texcoord = true;
   caps->max_varyings = 32;
   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->max_gs_invocations = 0;  /* stripped: no geometry shaders */
   caps->max_shader_buffer_size = 0;       /* stripped: no SSBOs */
   caps->shader_buffer_offset_alignment = 0;
   caps->image_store_formatted = false;    /* stripped: no image load/store */

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 255.0; /* arbitrary */
   caps->max_point_size =
   caps->max_point_size_aa = 255.0; /* arbitrary */
   caps->max_texture_anisotropy = 16.0;
   caps->max_texture_lod_bias = 16.0; /* arbitrary */

}

struct pipe_screen *mypipe_create_screen(struct sw_winsys *winsys){
    fprintf(stderr, "STUB: mypipe_create_screen\n");
    struct mypipe_screen *screen = CALLOC_STRUCT(mypipe_screen);

    if(!screen)
        return NULL;

    screen->winsys = winsys;

    screen->base.destroy = mypipe_destroy_screen;
    screen->base.get_name = mypipe_get_name;
    screen->base.get_vendor = mypipe_get_vendor;
    screen->base.get_device_vendor = mypipe_get_device_vendor;
    screen->base.get_screen_fd = mypipe_get_fd;
    screen->base.get_timestamp = mypipe_get_timestamp;
    screen->base.query_memory_info = mypipe_sw_query_memory_info;
    screen->base.is_format_supported = mypipe_is_format_supported;
    screen->base.context_create = mypipe_create_context;
    screen->base.flush_frontbuffer = mypipe_flush_frontbuffer;

    screen->base.nir_options[MESA_SHADER_VERTEX] = &mp_compiler_options;
    screen->base.nir_options[MESA_SHADER_FRAGMENT] = &mp_compiler_options;

    mypipe_init_screen_texture_funcs(&screen->base);
    mypipe_init_screen_fence_funcs(&screen->base);

    mypipe_init_shader_caps(screen);
    mypipe_init_screen_caps(screen);

    return &screen->base;
}
