#ifndef MP_CONTEXT_H
#define MP_CONTEXT_H

#include "pipe/p_context.h"

struct mypipe_context {
    struct pipe_context pipe;
    unsigned int hello;
};

static inline struct mypipe_context * mypipe_context(struct pipe_context * pipe){
    return (struct mypipe_context*)pipe;
}

struct pipe_context *mypipe_create_context(struct pipe_screen * , void *priv, unsigned int flags);

#endif