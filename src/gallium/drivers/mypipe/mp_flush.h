#ifndef MP_FLUSH_H
#define MP_FLUSH_H

#include "util/compiler.h"
#include "pipe/p_context.h"

struct pipe_context;
struct pipe_fence_handle;

// void mypipe_flush(struct pipe_context);

// void mypipe_flush_wrapped();

bool mypipe_flush_resource(struct pipe_context *pipe,
                           struct pipe_resource *texture,
                           unsigned level,
                           int layer,
                           unsigned flush_flags,
                           bool read_only,
                           bool cpu_access,
                           bool do_not_block);

// void mypipe_texture_barrier();
// void mypipe_memory_barrier();

#endif