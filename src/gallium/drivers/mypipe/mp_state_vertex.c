#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_create_vertex_elements_state(struct pipe_context *pipe,
                                                 unsigned num_elements,
                                                 const struct pipe_vertex_element *attribs){
    fprintf(stderr, "STUB: mypipe_create_vertex_elements_state\n");
    return NULL;
}

static void mypipe_bind_vertex_elements_state(struct pipe_context *pipe, void *velems){
    fprintf(stderr, "STUB: mypipe_bind_vertex_elements_state\n");
}

static void mypipe_delete_vertex_elements_state(struct pipe_context *pipe, void *velems){
    fprintf(stderr, "STUB: mypipe_delete_vertex_elements_state\n");
}

static void mypipe_set_vertex_buffers(struct pipe_context *pipe,
                                      unsigned count,
                                      const struct pipe_vertex_buffer *buffers){
    fprintf(stderr, "STUB: mypipe_set_vertex_buffers\n");
}

void mypipe_init_vertex_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_vertex_funcs\n");
    pipe->create_vertex_elements_state = mypipe_create_vertex_elements_state;
    pipe->bind_vertex_elements_state = mypipe_bind_vertex_elements_state;
    pipe->delete_vertex_elements_state = mypipe_delete_vertex_elements_state;
    pipe->set_vertex_buffers = mypipe_set_vertex_buffers;
}
