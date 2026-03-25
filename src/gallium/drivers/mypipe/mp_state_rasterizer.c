#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "mp_state.h"
#include "mp_context.h"

static void *mypipe_create_rasterizer_state(struct pipe_context *pipe,
                                            const struct pipe_rasterizer_state *rasterizer){
    fprintf(stderr, "mypipe_create_rasterizer_state: fill_front=%d fill_back=%d cull_face=%d front_ccw=%d flatshade=%d flatshade_first=%d\n",
            rasterizer->fill_front, rasterizer->fill_back, rasterizer->cull_face, rasterizer->front_ccw,
            rasterizer->flatshade, rasterizer->flatshade_first);
    struct pipe_rasterizer_state *state = CALLOC_STRUCT(pipe_rasterizer_state);
    if (state)
        memcpy(state, rasterizer, sizeof(*state));
    return state;
}

static void mypipe_bind_rasterizer_state(struct pipe_context *pipe, void *rasterizer){
    mypipe_context(pipe)->rasterizer = (struct pipe_rasterizer_state *)rasterizer;
}

static void mypipe_delete_rasterizer_state(struct pipe_context *pipe, void *rasterizer){
    FREE(rasterizer);
}

void mypipe_init_rasterizer_funcs(struct pipe_context *pipe){
    pipe->create_rasterizer_state = mypipe_create_rasterizer_state;
    pipe->bind_rasterizer_state = mypipe_bind_rasterizer_state;
    pipe->delete_rasterizer_state = mypipe_delete_rasterizer_state;
}
