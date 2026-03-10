#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void mypipe_set_clip_state(struct pipe_context *pipe,
                                  const struct pipe_clip_state *clip){
    fprintf(stderr, "STUB: mypipe_set_clip_state\n");
}

static void mypipe_set_viewport_states(struct pipe_context *pipe,
                                       unsigned start_slot, unsigned num_viewports,
                                       const struct pipe_viewport_state *viewport){
    fprintf(stderr, "STUB: mypipe_set_viewport_states\n");
}

static void mypipe_set_scissor_states(struct pipe_context *pipe,
                                      unsigned start_slot, unsigned num_scissors,
                                      const struct pipe_scissor_state *scissor){
    fprintf(stderr, "STUB: mypipe_set_scissor_states\n");
}

static void mypipe_set_polygon_stipple(struct pipe_context *pipe,
                                       const struct pipe_poly_stipple *stipple){
    fprintf(stderr, "STUB: mypipe_set_polygon_stipple\n");
}

void mypipe_init_clip_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_clip_funcs\n");
    pipe->set_clip_state = mypipe_set_clip_state;
    pipe->set_viewport_states = mypipe_set_viewport_states;
    pipe->set_scissor_states = mypipe_set_scissor_states;
    pipe->set_polygon_stipple = mypipe_set_polygon_stipple;
}
