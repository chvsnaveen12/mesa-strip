#include "draw/draw_context.h"

#include "mp_screen.h"
#include "mp_context.h"

static void mypipe_destroy(struct pipe_context *pipe){
    return;
}

struct pipe_context *mypipe_create_context(struct pipe_screen *screen, void *priv, unsigned int flags){
    // assert(("create context", false));
    struct mypipe_screen *mp_screen = mypipe_screen(screen);
    struct mypipe_context *mypipe = CALLOC_STRUCT(mypipe_context);

    util_init_math();

    mypipe->pipe.screen = screen;
    mypipe->pipe.destroy = mypipe_destroy;
    mypipe->pipe.priv = priv;

    

    return &mypipe->pipe;
}
