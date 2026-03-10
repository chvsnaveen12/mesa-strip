#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static struct pipe_stream_output_target *mypipe_create_stream_output_target(
                        struct pipe_context *pipe,
                        struct pipe_resource *res,
                        unsigned buffer_offset,
                        unsigned buffer_size){
    fprintf(stderr, "STUB: mypipe_create_stream_output_target\n");
    return NULL;
}

static void mypipe_stream_output_target_destroy(struct pipe_context *pipe,
                                                struct pipe_stream_output_target *target){
    fprintf(stderr, "STUB: mypipe_stream_output_target_destroy\n");
}

static void mypipe_set_stream_output_targets(struct pipe_context *pipe,
                                             unsigned num_targets,
                                             struct pipe_stream_output_target **targets,
                                             const unsigned *offsets,
                                             enum mesa_prim output_prim){
    fprintf(stderr, "STUB: mypipe_set_stream_output_targets\n");
}

void mypipe_init_streamout_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_streamout_funcs\n");
    pipe->create_stream_output_target = mypipe_create_stream_output_target;
    pipe->stream_output_target_destroy = mypipe_stream_output_target_destroy;
    pipe->set_stream_output_targets = mypipe_set_stream_output_targets;
}
