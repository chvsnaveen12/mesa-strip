#ifndef MP_CONTEXT_H
#define MP_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#define MP_MAX_ATTRIBS 16

struct mp_vertex_element_state {
    unsigned num_elements;
    struct pipe_vertex_element elements[MP_MAX_ATTRIBS];
};

// struct mp_compiled_shader {
//     nir_shader *nir;
// };

struct mypipe_context {
    struct pipe_context pipe;
    struct pipe_framebuffer_state framebuffer;
  
    struct pipe_viewport_state viewport;
    struct pipe_vertex_buffer vertex_buffers[MP_MAX_ATTRIBS];
    unsigned num_vertex_buffers;
    struct mp_vertex_element_state *velems;

    unsigned int hello;
};

static inline struct mypipe_context * mypipe_context(struct pipe_context * pipe){
    return (struct mypipe_context*)pipe;
}

struct pipe_context *mypipe_create_context(struct pipe_screen * , void *priv, unsigned int flags);

#endif