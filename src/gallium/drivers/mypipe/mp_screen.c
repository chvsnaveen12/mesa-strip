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

    if(target != PIPE_BUFFER && target != PIPE_TEXTURE_2D &&
       target != PIPE_TEXTURE_CUBE && target != PIPE_TEXTURE_RECT)
        return false;

    /* Strip display/scanout/shared — check with winsys, then check remaining */
    unsigned check = bind;
    if(check & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)){
        struct sw_winsys *winsys = mypipe_screen(screen)->winsys;
        if(!winsys->is_displaytarget_format_supported(winsys, bind, format))
            return false;
        check &= ~(PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);
        if (!check)
            return true;
    }

    /* Check each remaining bind flag — format must pass ALL */
    if(check & PIPE_BIND_RENDER_TARGET){
        switch (format){
            case PIPE_FORMAT_B8G8R8A8_UNORM:
            case PIPE_FORMAT_B8G8R8X8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
            case PIPE_FORMAT_R8G8B8X8_UNORM:
                check &= ~PIPE_BIND_RENDER_TARGET;
                break;
            default:
                return false;
        }
    }
    if(check & PIPE_BIND_DEPTH_STENCIL){
        switch(format){
            case PIPE_FORMAT_Z24_UNORM_S8_UINT:
            case PIPE_FORMAT_S8_UINT_Z24_UNORM:
            case PIPE_FORMAT_Z24X8_UNORM:
            case PIPE_FORMAT_X8Z24_UNORM:
            case PIPE_FORMAT_Z16_UNORM:
            case PIPE_FORMAT_Z32_FLOAT:
            case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
            case PIPE_FORMAT_S8_UINT:
                check &= ~PIPE_BIND_DEPTH_STENCIL;
                break;
            default:
                return false;
        }
    }
    if(check & PIPE_BIND_SAMPLER_VIEW){
        switch (format){
            /* Color formats */
            case PIPE_FORMAT_B8G8R8A8_UNORM:
            case PIPE_FORMAT_B8G8R8X8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
            case PIPE_FORMAT_R8G8B8X8_UNORM:
            case PIPE_FORMAT_R8_UNORM:
            case PIPE_FORMAT_R8G8_UNORM:
            case PIPE_FORMAT_A8_UNORM:
            case PIPE_FORMAT_L8_UNORM:
            case PIPE_FORMAT_L8A8_UNORM:
            case PIPE_FORMAT_R32G32B32A32_FLOAT:
            case PIPE_FORMAT_R32_FLOAT:
            case PIPE_FORMAT_R32G32_FLOAT:
            case PIPE_FORMAT_R32G32B32_FLOAT:
            case PIPE_FORMAT_B8G8R8A8_SRGB:
            case PIPE_FORMAT_R8G8B8A8_SRGB:
            /* Depth formats as sampler view (shadow mapping, depth readback) */
            case PIPE_FORMAT_Z24_UNORM_S8_UINT:
            case PIPE_FORMAT_S8_UINT_Z24_UNORM:
            case PIPE_FORMAT_Z24X8_UNORM:
            case PIPE_FORMAT_X8Z24_UNORM:
            case PIPE_FORMAT_Z16_UNORM:
            case PIPE_FORMAT_Z32_FLOAT:
            case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                check &= ~PIPE_BIND_SAMPLER_VIEW;
                break;
            default:
                return false;
        }
    }
    if(check & (PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_INDEX_BUFFER))
        return true;

    /* If we checked at least one bind above and didn't return false, accept it.
     * Unknown bind flags (BLENDABLE, LINEAR, etc.) are ignored — we're software. */
    return (bind != check) || (bind == 0);
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
    static int frame_count = 0;
    frame_count++;
    struct mypipe_screen *screen = mypipe_screen(_screen);
    struct sw_winsys *winsys = screen->winsys;
    struct mypipe_resource *texture = mypipe_resource(resource);
    assert(texture->dt);

    /* Copy shadow buffer → display target, then present */
    if (texture->dt && texture->data) {
        uint8_t *dt_map = winsys->displaytarget_map(winsys, texture->dt, PIPE_MAP_WRITE);
        if (dt_map) {
            unsigned stride = texture->stride[0];
            unsigned h = resource->height0;
            memcpy(dt_map, texture->data, (size_t)stride * h);
            winsys->displaytarget_unmap(winsys, texture->dt);
        }
    }

    if(texture->dt)
        winsys->displaytarget_display(winsys, texture->dt, context_private, nboxes, sub_box);

    /* Debug: dump framebuffer to PPM at frame 50 */
    if (frame_count == 200 && texture->data) {
        unsigned w = resource->width0, h = resource->height0;
        unsigned stride = texture->stride[0];
        FILE *f = fopen("/tmp/mypipe_frame50.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (unsigned y = 0; y < h; y++) {
                uint8_t *row = (uint8_t*)texture->data + y * stride;
                for (unsigned x = 0; x < w; x++) {
                    /* Assume B8G8R8A8 or B8G8R8X8 */
                    fputc(row[x*4+2], f); /* R */
                    fputc(row[x*4+1], f); /* G */
                    fputc(row[x*4+0], f); /* B */
                }
            }
            fclose(f);
            fprintf(stderr, ">>> DUMPED FRAME 50 to /tmp/mypipe_frame50.ppm <<<\n");
        }
    }
}

static const nir_shader_compiler_options mp_compiler_options = {
0
};

static void mypipe_init_shader_caps(struct mypipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_init_shader_caps\n");
    static const unsigned stages[] = {MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT};

    for(int i = 0; i < 2; i++){
        struct pipe_shader_caps *caps = &screen->base.shader_caps[stages[i]];

        /* ES 2.0 level shader caps */
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
        caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
        caps->max_shader_buffers = 0;
        caps->max_shader_images = 0;
    }
}

static void mypipe_init_screen_caps(struct mypipe_screen *mp_screen){
    fprintf(stderr, "STUB: mypipe_init_screen_caps\n");
    struct pipe_caps *caps = &mp_screen->base.caps;

    u_init_pipe_screen_caps(&mp_screen->base, 0);
   /* ---- GL ES 2.0 / desktop GL 2.0 level caps ---- */
   /* Tell Mesa we handle alpha test and flat shading natively in the rasterizer,
    * so it doesn't try to lower them to shader variants (which we don't support). */
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
   caps->image_atomic_float_add = false;
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
   caps->glsl_feature_level_compatibility = 120;  /* GLSL 1.20 = GL 2.1 / ES 2.0 */
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
