#include "mp_flush.h"
#include "mp_context.h"
#include "mp_state.h"

#include "pipe/p_state.h"

bool mypipe_flush_resource(struct pipe_context *pipe,
                           struct pipe_resource *texture,
                           unsigned level,
                           int layer,
                           unsigned flush_flags,
                           bool read_only,
                           bool cpu_access,
                           bool do_not_block){
    fprintf(stderr, "STUB: mypipe_flush_resource\n");
    return true;
}