#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "mp_state.h"
#include "mp_context.h"

static void *mypipe_create_blend_state(struct pipe_context *pipe,
                                       const struct pipe_blend_state *blend){
    fprintf(stderr, "mypipe_create_blend_state: rt0 blend_enable=%d rgb_func=%d rgb_src=%d rgb_dst=%d alpha_src=%d alpha_dst=%d colormask=0x%x\n",
            blend->rt[0].blend_enable, blend->rt[0].rgb_func,
            blend->rt[0].rgb_src_factor, blend->rt[0].rgb_dst_factor,
            blend->rt[0].alpha_src_factor, blend->rt[0].alpha_dst_factor,
            blend->rt[0].colormask);
    struct pipe_blend_state *state = CALLOC_STRUCT(pipe_blend_state);
    if (state)
        memcpy(state, blend, sizeof(*state));
    return state;
}

static void mypipe_bind_blend_state(struct pipe_context *pipe, void *blend){
    mypipe_context(pipe)->blend = (struct pipe_blend_state *)blend;
}

static void mypipe_delete_blend_state(struct pipe_context *pipe, void *blend){
    FREE(blend);
}

static void mypipe_set_blend_color(struct pipe_context *pipe,
                                   const struct pipe_blend_color *blend_color){
    mypipe_context(pipe)->blend_color = *blend_color;
}

static void *mypipe_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                                     const struct pipe_depth_stencil_alpha_state *templ){
    fprintf(stderr, "mypipe_create_depth_stencil_alpha_state: depth_enabled=%d depth_writemask=%d depth_func=%d alpha_enabled=%d alpha_func=%d alpha_ref=%.2f\n",
            templ->depth_enabled, templ->depth_writemask, templ->depth_func,
            templ->alpha_enabled, templ->alpha_func, templ->alpha_ref_value);
    struct pipe_depth_stencil_alpha_state *state = CALLOC_STRUCT(pipe_depth_stencil_alpha_state);
    if (state)
        memcpy(state, templ, sizeof(*state));
    return state;
}

static void mypipe_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *depth_stencil){
    mypipe_context(pipe)->depth_stencil = (struct pipe_depth_stencil_alpha_state *)depth_stencil;
}

static void mypipe_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *depth_stencil){
    FREE(depth_stencil);
}

static void mypipe_set_stencil_ref(struct pipe_context *pipe,
                                   const struct pipe_stencil_ref stencil_ref){
    mypipe_context(pipe)->stencil_ref = stencil_ref;
}

static void mypipe_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask){
}

void mypipe_init_blend_funcs(struct pipe_context *pipe){
    pipe->create_blend_state = mypipe_create_blend_state;
    pipe->bind_blend_state = mypipe_bind_blend_state;
    pipe->delete_blend_state = mypipe_delete_blend_state;
    pipe->set_blend_color = mypipe_set_blend_color;
    pipe->create_depth_stencil_alpha_state = mypipe_create_depth_stencil_alpha_state;
    pipe->bind_depth_stencil_alpha_state = mypipe_bind_depth_stencil_alpha_state;
    pipe->delete_depth_stencil_alpha_state = mypipe_delete_depth_stencil_alpha_state;
    pipe->set_stencil_ref = mypipe_set_stencil_ref;
    pipe->set_sample_mask = mypipe_set_sample_mask;
}
