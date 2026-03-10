#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static struct pipe_query *mypipe_create_query(struct pipe_context *pipe,
                                              unsigned query_type,
                                              unsigned index){
    fprintf(stderr, "STUB: mypipe_create_query\n");
    return NULL;
}

static void mypipe_destroy_query(struct pipe_context *pipe,
                                 struct pipe_query *q){
    fprintf(stderr, "STUB: mypipe_destroy_query\n");
}

static bool mypipe_begin_query(struct pipe_context *pipe, struct pipe_query *q){
    fprintf(stderr, "STUB: mypipe_begin_query\n");
    return false;
}

static bool mypipe_end_query(struct pipe_context *pipe, struct pipe_query *q){
    fprintf(stderr, "STUB: mypipe_end_query\n");
    return false;
}

static bool mypipe_get_query_result(struct pipe_context *pipe,
                                    struct pipe_query *q,
                                    bool wait,
                                    union pipe_query_result *result){
    fprintf(stderr, "STUB: mypipe_get_query_result\n");
    return false;
}

static void mypipe_set_active_query_state(struct pipe_context *pipe, bool enable){
    fprintf(stderr, "STUB: mypipe_set_active_query_state\n");
}

void mypipe_init_query_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_query_funcs\n");
    pipe->create_query = mypipe_create_query;
    pipe->destroy_query = mypipe_destroy_query;
    pipe->begin_query = mypipe_begin_query;
    pipe->end_query = mypipe_end_query;
    pipe->get_query_result = mypipe_get_query_result;
    pipe->set_active_query_state = mypipe_set_active_query_state;
}
