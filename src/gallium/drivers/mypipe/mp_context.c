#include <stdio.h>
#include <stdbool.h>
#include "draw/draw_context.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_pack_color.h"

#include "mp_texture.h"
#include "mp_screen.h"
#include "mp_context.h"
#include "mp_state.h"

#include "frontend/sw_winsys.h"

static void mypipe_destroy(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_destroy\n");
}

static void mypipe_draw_vbo(struct pipe_context *pipe,
                            const struct pipe_draw_info *info,
                            unsigned drawid_offset,
                            const struct pipe_draw_indirect_info *indirect,
                            const struct pipe_draw_start_count_bias *draws,
                            unsigned num_draws){
    fprintf(stderr, "STUB: mypipe_draw_vbo: mode=%u num_draws=%u", info->mode, num_draws);
    for (unsigned i = 0; i < num_draws; i++)
        fprintf(stderr, " draw[%u]={start=%u count=%u}", i, draws[i].start, draws[i].count);
    fprintf(stderr, " indexed=%d\n", info->index_size > 0);
}

static void mypipe_clear(struct pipe_context *pipe,
                         unsigned buffers,
                         uint32_t color_clear_mask,
                         uint8_t stencil_clear_mask,
                         const struct pipe_scissor_state *scissor_state,
                         const union pipe_color_union *color,
                         double depth,
                         unsigned stencil){
    fprintf(stderr, "STUB: mypipe_clear: buffers=0x%x", buffers);
    if (color) fprintf(stderr, " color=(%.2f,%.2f,%.2f,%.2f)", color->f[0], color->f[1], color->f[2], color->f[3]);
    if (buffers & 0x2) fprintf(stderr, " depth=%.2f", depth);
    fprintf(stderr, "\n");

    struct mypipe_context *mypipe = mypipe_context(pipe);
    struct pipe_surface *zsbuf = & mypipe->framebuffer.zsbuf;
    unsigned zs_buffers = buffers & PIPE_CLEAR_DEPTHSTENCIL;

    uint64_t cv;
    uint i;

    if(buffers & PIPE_CLEAR_COLOR){
        for(i = 0; i < mypipe->framebuffer.nr_cbufs; i++){
            if(buffers & (PIPE_CLEAR_COLOR0 << i)){
                // I don't know what to do here FIXME TILE CACHE
                struct pipe_surface *ps = &mypipe->framebuffer.cbufs[i];
                struct mypipe_resource *mpr = mypipe_resource(ps->texture);
                struct sw_winsys *winsys = mypipe_screen(pipe->screen)->winsys;
                
                uint8_t *map;
                if(mpr->dt)
                    map = winsys->displaytarget_map(winsys, mpr->dt, PIPE_MAP_WRITE);
                else
                    map = mpr->data;
                
                uint32_t pixel;
                util_pack_color_ub((uint8_t)(color->f[0] * 255),
                                   (uint8_t)(color->f[1] * 255),
                                   (uint8_t)(color->f[2] * 255),
                                   (uint8_t)(color->f[3] * 255),
                                   ps->format, (union util_color *)&pixel);
                unsigned stride = mpr->stride[0];

                for(unsigned y = 0; y < mypipe->framebuffer.height; y++){
                    uint32_t *row = (uint32_t *)(map + y * stride);
                    for(unsigned x = 0; x < mypipe->framebuffer.width; x++){
                        row[x] = pixel;
                    }
                }
                if(mpr->dt)
                    winsys->displaytarget_unmap(winsys, mpr->dt);
            }
        }
    }

    if(zs_buffers && util_format_is_depth_and_stencil(zsbuf->texture->format) && zs_buffers != PIPE_CLEAR_DEPTHSTENCIL){
        util_clear_depth_stencil(pipe, zsbuf, zs_buffers, depth, stencil, 0, 0, pipe_surface_width(zsbuf), pipe_surface_height(zsbuf));        
    }
    else if(zs_buffers){
        static const union pipe_color_union zero;
        cv = util_pack64_z_stencil(zsbuf->format, depth, stencil);
        // Again, FIXME, tile cache
    }
}

static void mypipe_flush(struct pipe_context *pipe,
                         struct pipe_fence_handle **fence,
                         unsigned flags){
    fprintf(stderr, "STUB: mypipe_flush\n");
}

static void mypipe_set_framebuffer_state(struct pipe_context *pipe,
                                         const struct pipe_framebuffer_state *framebuffer){
    fprintf(stderr, "STUB: mypipe_set_framebuffer_state: %ux%u nr_cbufs=%u zsbuf_tex=%p\n",
            framebuffer->width, framebuffer->height,
            framebuffer->nr_cbufs, (void*)framebuffer->zsbuf.texture);
    struct mypipe_context *mypipe = mypipe_context(pipe);
    mypipe->framebuffer = *framebuffer;
    for (unsigned i = 0; i < framebuffer->nr_cbufs; i++) {
        const struct pipe_surface *s = &framebuffer->cbufs[i];
        if (s->texture)
            fprintf(stderr, "  cbuf[%u]: format=%d texture=%p\n", i, s->format, (void*)s->texture);
    }
}

static void mypipe_set_debug_callback(struct pipe_context *pipe,
                                      const struct util_debug_callback *cb){
    fprintf(stderr, "STUB: mypipe_set_debug_callback\n");
}

static void mypipe_texture_barrier(struct pipe_context *pipe, unsigned flags){
    fprintf(stderr, "STUB: mypipe_texture_barrier\n");
}

static void mypipe_memory_barrier(struct pipe_context *pipe, unsigned flags){
    fprintf(stderr, "STUB: mypipe_memory_barrier\n");
}

static void mypipe_render_condition(struct pipe_context *pipe,
                                    struct pipe_query *query,
                                    bool condition,
                                    enum pipe_render_cond_flag mode){
    fprintf(stderr, "STUB: mypipe_render_condition\n");
}

struct pipe_context *mypipe_create_context(struct pipe_screen *screen, void *priv, unsigned int flags){
    fprintf(stderr, "STUB: mypipe_create_context\n");
    struct mypipe_screen *mp_screen = mypipe_screen(screen);
    struct mypipe_context *mypipe = CALLOC_STRUCT(mypipe_context);

    util_init_math();

    mypipe->pipe.screen = screen;
    mypipe->pipe.destroy = mypipe_destroy;
    mypipe->pipe.priv = priv;

    mypipe->pipe.draw_vbo = mypipe_draw_vbo;
    mypipe->pipe.clear = mypipe_clear;
    mypipe->pipe.flush = mypipe_flush;
    mypipe->pipe.set_framebuffer_state = mypipe_set_framebuffer_state;
    mypipe->pipe.set_debug_callback = mypipe_set_debug_callback;
    mypipe->pipe.texture_barrier = mypipe_texture_barrier;
    mypipe->pipe.memory_barrier = mypipe_memory_barrier;
    mypipe->pipe.render_condition = mypipe_render_condition;

    mypipe_init_blend_funcs(&mypipe->pipe);
    mypipe_init_clip_funcs(&mypipe->pipe);
    mypipe_init_rasterizer_funcs(&mypipe->pipe);
    mypipe_init_sampler_funcs(&mypipe->pipe);
    mypipe_init_shader_funcs(&mypipe->pipe);
    mypipe_init_streamout_funcs(&mypipe->pipe);
    mypipe_init_context_texture_funcs(&mypipe->pipe);
    mypipe_init_vertex_funcs(&mypipe->pipe);
    mypipe_init_image_funcs(&mypipe->pipe);
    mypipe_init_query_funcs(&mypipe->pipe);

    return &mypipe->pipe;
}
