#include <stdio.h>
#include "mp_screen.h"
#include "mp_fence.h"

static void mypipe_fence_reference(struct pipe_screen *screen,
                                   struct pipe_fence_handle **ptr,
                                   struct pipe_fence_handle *fence){
    *ptr = fence;
}

static bool mypipe_fence_finish(struct pipe_screen *screen,
                                struct pipe_context *ctx,
                                struct pipe_fence_handle *fence,
                                uint64_t timeout){
    /* Software driver — everything is synchronous, always done */
    return true;
}

void mypipe_init_screen_fence_funcs(struct pipe_screen *screen){
    screen->fence_reference = mypipe_fence_reference;
    screen->fence_finish = mypipe_fence_finish;
}
