#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_create_blend_state(struct pipe_context *pipe,
                                       const struct pipe_blend_state *blend){
    fprintf(stderr, "STUB: mypipe_create_blend_state\n");
    return NULL;
}

static void mypipe_bind_blend_state(struct pipe_context *pipe, void *blend){
    fprintf(stderr, "STUB: mypipe_bind_blend_state\n");
}

static void mypipe_delete_blend_state(struct pipe_context *pipe, void *blend){
    fprintf(stderr, "STUB: mypipe_delete_blend_state\n");
}

static void mypipe_set_blend_color(struct pipe_context *pipe,
                                   const struct pipe_blend_color *blend_color){
    fprintf(stderr, "STUB: mypipe_set_blend_color\n");
}

static void *mypipe_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                                     const struct pipe_depth_stencil_alpha_state *templ){
    fprintf(stderr, "STUB: mypipe_create_depth_stencil_alpha_state\n");
    return NULL;
}

static void mypipe_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *depth_stencil){
    fprintf(stderr, "STUB: mypipe_bind_depth_stencil_alpha_state\n");
}

static void mypipe_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *depth_stencil){
    fprintf(stderr, "STUB: mypipe_delete_depth_stencil_alpha_state\n");
}

static void mypipe_set_stencil_ref(struct pipe_context *pipe,
                                   const struct pipe_stencil_ref stencil_ref){
    fprintf(stderr, "STUB: mypipe_set_stencil_ref\n");
}

static void mypipe_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask){
    fprintf(stderr, "STUB: mypipe_set_sample_mask\n");
}

void mypipe_init_blend_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_blend_funcs\n");
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
