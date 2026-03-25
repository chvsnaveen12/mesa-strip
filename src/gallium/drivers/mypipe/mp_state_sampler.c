#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "mp_state.h"
#include "mp_context.h"

static void *mypipe_create_sampler_state(struct pipe_context *pipe,
                                         const struct pipe_sampler_state *sampler){
    struct pipe_sampler_state *state = CALLOC_STRUCT(pipe_sampler_state);
    if (state)
        memcpy(state, sampler, sizeof(*state));
    return state;
}

static void mypipe_bind_sampler_states(struct pipe_context *pipe,
                                       mesa_shader_stage shader,
                                       unsigned start_slot, unsigned num_samplers,
                                       void **samplers){
    struct mypipe_context *ctx = mypipe_context(pipe);
    for (unsigned i = 0; i < num_samplers; i++) {
        unsigned slot = start_slot + i;
        if (slot < MP_MAX_SAMPLERS)
            ctx->samplers[shader][slot] = (struct pipe_sampler_state *)
                (samplers ? samplers[i] : NULL);
    }
    ctx->num_samplers[shader] = start_slot + num_samplers;
}

static void mypipe_delete_sampler_state(struct pipe_context *pipe, void *sampler){
    FREE(sampler);
}

static struct pipe_sampler_view *mypipe_create_sampler_view(struct pipe_context *pipe,
                                                            struct pipe_resource *resource,
                                                            const struct pipe_sampler_view *templ){
    struct pipe_sampler_view *view = CALLOC_STRUCT(pipe_sampler_view);
    if (!view) return NULL;

    *view = *templ;
    view->texture = NULL;  /* clear before pipe_resource_reference unrefs old */
    view->context = pipe;
    pipe_reference_init(&view->reference, 1);
    pipe_resource_reference(&view->texture, resource);
    return view;
}

static void mypipe_set_sampler_views(struct pipe_context *pipe,
                                     mesa_shader_stage shader,
                                     unsigned start_slot, unsigned num_views,
                                     unsigned unbind_num_trailing_slots,
                                     struct pipe_sampler_view **views){
    struct mypipe_context *ctx = mypipe_context(pipe);
    for (unsigned i = 0; i < num_views; i++) {
        unsigned slot = start_slot + i;
        if (slot < MP_MAX_SAMPLERS)
            pipe_sampler_view_reference(&ctx->sampler_views[shader][slot],
                                        views ? views[i] : NULL);
    }
    for (unsigned i = 0; i < unbind_num_trailing_slots; i++) {
        unsigned slot = start_slot + num_views + i;
        if (slot < MP_MAX_SAMPLERS)
            pipe_sampler_view_reference(&ctx->sampler_views[shader][slot], NULL);
    }
    ctx->num_sampler_views[shader] = start_slot + num_views;
}

static void mypipe_sampler_view_destroy(struct pipe_context *pipe,
                                        struct pipe_sampler_view *view){
    pipe_resource_reference(&view->texture, NULL);
    FREE(view);
}

void mypipe_init_sampler_funcs(struct pipe_context *pipe){
    pipe->create_sampler_state = mypipe_create_sampler_state;
    pipe->bind_sampler_states = mypipe_bind_sampler_states;
    pipe->delete_sampler_state = mypipe_delete_sampler_state;
    pipe->create_sampler_view = mypipe_create_sampler_view;
    pipe->set_sampler_views = mypipe_set_sampler_views;
    pipe->sampler_view_destroy = mypipe_sampler_view_destroy;
    pipe->sampler_view_release = u_default_sampler_view_release;
}
