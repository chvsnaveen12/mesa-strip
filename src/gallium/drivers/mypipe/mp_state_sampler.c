#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_create_sampler_state(struct pipe_context *pipe,
                                         const struct pipe_sampler_state *sampler){
    fprintf(stderr, "STUB: mypipe_create_sampler_state\n");
    return NULL;
}

static void mypipe_bind_sampler_states(struct pipe_context *pipe,
                                       mesa_shader_stage shader,
                                       unsigned start_slot, unsigned num_samplers,
                                       void **samplers){
    fprintf(stderr, "STUB: mypipe_bind_sampler_states\n");
}

static void mypipe_delete_sampler_state(struct pipe_context *pipe, void *sampler){
    fprintf(stderr, "STUB: mypipe_delete_sampler_state\n");
}

static struct pipe_sampler_view *mypipe_create_sampler_view(struct pipe_context *pipe,
                                                            struct pipe_resource *resource,
                                                            const struct pipe_sampler_view *templ){
    fprintf(stderr, "STUB: mypipe_create_sampler_view\n");
    return NULL;
}

static void mypipe_set_sampler_views(struct pipe_context *pipe,
                                     mesa_shader_stage shader,
                                     unsigned start_slot, unsigned num_views,
                                     unsigned unbind_num_trailing_slots,
                                     struct pipe_sampler_view **views){
    fprintf(stderr, "STUB: mypipe_set_sampler_views\n");
}

static void mypipe_sampler_view_destroy(struct pipe_context *pipe,
                                        struct pipe_sampler_view *view){
    fprintf(stderr, "STUB: mypipe_sampler_view_destroy\n");
}

void mypipe_init_sampler_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_sampler_funcs\n");
    pipe->create_sampler_state = mypipe_create_sampler_state;
    pipe->bind_sampler_states = mypipe_bind_sampler_states;
    pipe->delete_sampler_state = mypipe_delete_sampler_state;
    pipe->create_sampler_view = mypipe_create_sampler_view;
    pipe->set_sampler_views = mypipe_set_sampler_views;
    pipe->sampler_view_destroy = mypipe_sampler_view_destroy;
}
