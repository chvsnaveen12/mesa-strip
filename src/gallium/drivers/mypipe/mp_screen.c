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
    assert(("Destroy", false));
    FREE(screen);
}

static const char * mypipe_get_name(struct pipe_screen *screen){
    assert(("Name", false));
    return "mypipe";
}

static const char * mypipe_get_vendor(struct pipe_screen *screen){
    assert(("Vendor", false));
    return "Naveen";
}

static int mypipe_get_fd(struct pipe_screen *screen){
    assert(("FD", false));

    struct sw_winsys *winsys = mypipe_screen(screen)->winsys;
    if(winsys->get_fd)
        return winsys->get_fd(winsys);
    else
        return -1;
}

static uint64_t mypipe_get_timestamp(struct pipe_screen *screen){
    assert(("time_stamp", false));
    return os_time_get_nano();
}

static void mypipe_sw_query_memory_info(struct pipe_screen *screen, struct pipe_memory_info *info){
    assert(("Mem info", false));
    return;
}

static bool mypipe_is_format_supported(struct pipe_screen *screen,
                                       enum pipe_format format,
                                       enum pipe_texture_target target,
                                       unsigned int sample_count,
                                       unsigned int storage_sample_count,
                                       unsigned int bind){
    // assert(("format sup", false));
    // struct sw_winsys *winsys = mypipe_screen(screen)->winsys;
    if(sample_count > 1)
        return false;
    
    if(target != PIPE_BUFFER && target != PIPE_TEXTURE_2D && target != PIPE_TEXTURE_CUBE)
        return false;

    if(bind & PIPE_BIND_RENDER_TARGET){
        // return true;
        switch (format){
            case PIPE_FORMAT_B8G8R8A8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
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
            case PIPE_FORMAT_R8G8B8A8_UNORM:
                return true;
            default:
                return false;
        }
    }
    if(bind & PIPE_BIND_VERTEX_BUFFER)
        return true;
    return false;
}

static void mypipe_flush_frontbuffer(struct pipe_screen * _screen,
                                     struct pipe_context *pipe,
                                     struct pipe_resource *resource,
                                     unsigned int level, unsigned int layer,
                                     void *context_private,
                                     unsigned nboxes, struct pipe_box *sub_box){
    assert(("Mem info", false));
    return;
}

static const nir_shader_compiler_options mp_compiler_options = {
0
};

static void mypipe_init_shader_caps(struct mypipe_screen *screen){
    static const unsigned stages[] = {MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT};

    for(int i = 0; i < 2; i++){
        struct pipe_shader_caps *caps = &screen->base.shader_caps[stages[i]];

        caps->max_instructions =
        caps->max_alu_instructions =
        caps->max_tex_instructions = 
        caps->max_tex_indirections = 4096;
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
    struct pipe_caps *caps = &mp_screen->base.caps;

    u_init_pipe_screen_caps(&mp_screen->base, 0);
}

struct pipe_screen *mypipe_create_screen(struct sw_winsys *winsys){
    struct mypipe_screen *screen = CALLOC_STRUCT(mypipe_screen);

    if(!screen)
        return NULL;
    
    screen->winsys = winsys;

    screen->base.destroy = mypipe_destroy_screen;
    screen->base.get_name = mypipe_get_name;
    screen->base.get_vendor = mypipe_get_vendor;
    screen->base.get_screen_fd = mypipe_get_fd;
    screen->base.get_timestamp = mypipe_get_timestamp;
    screen->base.query_memory_info = mypipe_sw_query_memory_info;
    screen->base.is_format_supported = mypipe_is_format_supported;
    screen->base.context_create = mypipe_create_context;
    screen->base.destroy = mypipe_destroy_screen;

    screen->base.nir_options[MESA_SHADER_VERTEX] = &mp_compiler_options;
    screen->base.nir_options[MESA_SHADER_FRAGMENT] = &mp_compiler_options;

    mypipe_init_screen_texture_funcs(&screen->base);
    mypipe_init_screen_fence_funcs(&screen->base);

    mypipe_init_shader_caps(screen);
    mypipe_init_screen_caps(screen);

    // assert(("Bruh moment", false));
    return &screen->base;
}