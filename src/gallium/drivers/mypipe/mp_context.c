#include <stdio.h>
#include <stdbool.h>
#include "draw/draw_context.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"

#include "mp_screen.h"
#include "mp_context.h"
#include "mp_state.h"

static void mypipe_destroy(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_destroy\n");
}

static void mypipe_draw_vbo(struct pipe_context *pipe,
                            const struct pipe_draw_info *info,
                            unsigned drawid_offset,
                            const struct pipe_draw_indirect_info *indirect,
                            const struct pipe_draw_start_count_bias *draws,
                            unsigned num_draws){
    fprintf(stderr, "STUB: mypipe_draw_vbo\n");
}

static void mypipe_clear(struct pipe_context *pipe,
                         unsigned buffers,
                         uint32_t color_clear_mask,
                         uint8_t stencil_clear_mask,
                         const struct pipe_scissor_state *scissor_state,
                         const union pipe_color_union *color,
                         double depth,
                         unsigned stencil){
    fprintf(stderr, "STUB: mypipe_clear\n");
}

static void mypipe_flush(struct pipe_context *pipe,
                         struct pipe_fence_handle **fence,
                         unsigned flags){
    fprintf(stderr, "STUB: mypipe_flush\n");
}

static void mypipe_set_framebuffer_state(struct pipe_context *pipe,
                                         const struct pipe_framebuffer_state *framebuffer){
    fprintf(stderr, "STUB: mypipe_set_framebuffer_state\n");
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
