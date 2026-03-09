#include "mp_screen.h"
#include "mp_texture.h"

static struct pipe_resource * mypipe_resource_create(struct pipe_screen * screen,
                                                     const struct pipe_resource *templat){
    assert(("mypipe_resource_create", false));
    return NULL;
}

static struct pipe_resource * mypipe_resource_create_front(struct pipe_screen *screen,
                                                            const struct pipe_resource *templat,
                                                            const void *map_from_private){
    assert(("mypipe_resource_create_front", false));
    return NULL;
}

static void mypipe_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *pt){
    assert(("mypipe_resource_destroy", false));
}

static struct pipe_resource * mypipe_resource_from_handle(struct pipe_screen *screen,
                                                          const struct pipe_resource *templat,
                                                          struct winsys_handle *whandle,
                                                          unsigned int usage){
    assert(("mypipe_resource_from_handle", false));
    return NULL;
}

static bool mypipe_resource_get_handle(struct pipe_screen *screen,
                                       struct pipe_context *ctx,
                                       struct pipe_resource *pt,
                                       struct winsys_handle *whandle,
                                       unsigned int usage){
    assert(("mypipe_resource_get_handle", false));
    return false;
}

static bool mypipe_can_create_resource(struct pipe_screen *screen, const struct pipe_resource *res){
    assert(("mypipe_can_create_resource", false));
    return false;
}

void mypipe_init_screen_texture_funcs(struct pipe_screen *screen){
    screen->resource_create = mypipe_resource_create;
    screen->resource_create_front = mypipe_resource_create_front;
    screen->resource_destroy = mypipe_resource_destroy;
    screen->resource_from_handle = mypipe_resource_from_handle;
    screen->resource_get_handle = mypipe_resource_get_handle;
    screen->can_create_resource = mypipe_can_create_resource;
    // assert(("Init texture funcs", false));
}