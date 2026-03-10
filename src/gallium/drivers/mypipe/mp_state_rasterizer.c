#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_create_rasterizer_state(struct pipe_context *pipe,
                                            const struct pipe_rasterizer_state *rasterizer){
    fprintf(stderr, "STUB: mypipe_create_rasterizer_state\n");
    return NULL;
}

static void mypipe_bind_rasterizer_state(struct pipe_context *pipe, void *rasterizer){
    fprintf(stderr, "STUB: mypipe_bind_rasterizer_state\n");
}

static void mypipe_delete_rasterizer_state(struct pipe_context *pipe, void *rasterizer){
    fprintf(stderr, "STUB: mypipe_delete_rasterizer_state\n");
}

void mypipe_init_rasterizer_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_rasterizer_funcs\n");
    pipe->create_rasterizer_state = mypipe_create_rasterizer_state;
    pipe->bind_rasterizer_state = mypipe_bind_rasterizer_state;
    pipe->delete_rasterizer_state = mypipe_delete_rasterizer_state;
}
