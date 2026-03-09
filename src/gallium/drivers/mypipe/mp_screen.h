#include "pipe/p_screen.h"
#include "pipe/p_defines.h"

struct sw_winsys;

struct mypipe_screen {
    struct pipe_screen base;
    struct sw_winsys *winsys;
};

static inline struct mypipe_screen *mypipe_screen(struct pipe_screen *pipe){
    return (struct mypipe_screen *)pipe;
}
