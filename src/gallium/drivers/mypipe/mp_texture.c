#include <stdio.h>
#include "mp_screen.h"
#include "mp_texture.h"

static struct pipe_resource * mypipe_resource_create(struct pipe_screen * screen,
                                                     const struct pipe_resource *templat){
    fprintf(stderr, "STUB: mypipe_resource_create\n");
    return NULL;
}

static struct pipe_resource * mypipe_resource_create_front(struct pipe_screen *screen,
                                                            const struct pipe_resource *templat,
                                                            const void *map_from_private){
    fprintf(stderr, "STUB: mypipe_resource_create_front\n");
    return NULL;
}

static void mypipe_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *pt){
    fprintf(stderr, "STUB: mypipe_resource_destroy\n");
}

static struct pipe_resource * mypipe_resource_from_handle(struct pipe_screen *screen,
                                                          const struct pipe_resource *templat,
                                                          struct winsys_handle *whandle,
                                                          unsigned int usage){
    fprintf(stderr, "STUB: mypipe_resource_from_handle\n");
    return NULL;
}

static bool mypipe_resource_get_handle(struct pipe_screen *screen,
                                       struct pipe_context *ctx,
                                       struct pipe_resource *pt,
                                       struct winsys_handle *whandle,
                                       unsigned int usage){
    fprintf(stderr, "STUB: mypipe_resource_get_handle\n");
    return false;
}

static bool mypipe_can_create_resource(struct pipe_screen *screen, const struct pipe_resource *res){
    fprintf(stderr, "STUB: mypipe_can_create_resource\n");
    return false;
}

void mypipe_init_screen_texture_funcs(struct pipe_screen *screen){
    fprintf(stderr, "STUB: mypipe_init_screen_texture_funcs\n");
    screen->resource_create = mypipe_resource_create;
    screen->resource_create_front = mypipe_resource_create_front;
    screen->resource_destroy = mypipe_resource_destroy;
    screen->resource_from_handle = mypipe_resource_from_handle;
    screen->resource_get_handle = mypipe_resource_get_handle;
    screen->can_create_resource = mypipe_can_create_resource;
}
