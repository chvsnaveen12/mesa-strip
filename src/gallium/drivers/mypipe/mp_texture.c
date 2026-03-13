#include <stdio.h>
#include "mp_screen.h"
#include "mp_texture.h"

#include "frontend/sw_winsys.h"

static const char *pipe_bind_str(unsigned bind) {
    static char buf[256];
    buf[0] = '\0';
    if (bind & PIPE_BIND_RENDER_TARGET) strcat(buf, "RT ");
    if (bind & PIPE_BIND_DEPTH_STENCIL) strcat(buf, "ZS ");
    if (bind & PIPE_BIND_SAMPLER_VIEW) strcat(buf, "SAMP ");
    if (bind & PIPE_BIND_VERTEX_BUFFER) strcat(buf, "VB ");
    if (bind & PIPE_BIND_INDEX_BUFFER) strcat(buf, "IB ");
    if (bind & PIPE_BIND_CONSTANT_BUFFER) strcat(buf, "CB ");
    if (bind & PIPE_BIND_DISPLAY_TARGET) strcat(buf, "DISP ");
    if (bind & PIPE_BIND_SCANOUT) strcat(buf, "SCAN ");
    if (bind & PIPE_BIND_SHARED) strcat(buf, "SHARED ");
    if (buf[0] == '\0') strcat(buf, "NONE");
    return buf;
}

static bool mypipe_displaytarget_layout(struct pipe_screen *screen,
                                        struct mypipe_resource * mpr,
                                        const void *map_front_private){
    struct sw_winsys *winsys = mypipe_screen(screen)->winsys;

    mpr->dt = winsys->displaytarget_create(winsys,
                                           mpr->base.bind,
                                           mpr->base.format,
                                           mpr->base.width0,
                                           mpr->base.height0,
                                           64,
                                           map_front_private,
                                           &mpr->stride[0]);
    return mpr->dt != NULL;

}

static bool mypipe_resource_layout(struct pipe_screen * screen, struct mypipe_resource *mpr, bool allocate){
    struct pipe_resource *pt = &mpr->base;
    unsigned level;
    unsigned width = pt->width0;
    unsigned height = pt->height0;
    unsigned depth = pt->depth0;
    uint64_t buffer_size = 0;

    if(pt->target == PIPE_BUFFER){
        mpr->stride[0] = pt->width0;
        if(allocate){
            mpr->data = align_malloc(pt->width0, 64);
            return mpr->data != NULL;
        }
        return true;
    }

    unsigned int slices = (pt->target == PIPE_TEXTURE_CUBE) ? 6 : 1;

    for (level = 0; level <= pt->last_level; level++){
        mpr->stride[level] = util_format_get_stride(pt->format, width);
        unsigned nblocksy = util_format_get_nblocksy(pt->format, height);

        mpr->img_stride[level] = mpr->stride[level] * nblocksy;
        mpr->level_offset[level] = buffer_size;

        buffer_size += (uint64_t)mpr->img_stride[level] * slices;

        width = MAX2(1, width >> 1);
        height = MAX2(1, height >> 1);
    }

    if(allocate){
        mpr->data = align_malloc(buffer_size, 64);
        return mpr->data != NULL;
    }
    return true;
}

static struct pipe_resource * mypipe_resource_create_front(struct pipe_screen *screen,
                                                            const struct pipe_resource *templat,
                                                            const void *map_from_private){
    fprintf(stderr, "STUB: mypipe_resource_create_front: target=%d format=%d %dx%dx%d bind=[%s]\n",
            templat->target, templat->format,
            templat->width0, templat->height0, templat->depth0,
            pipe_bind_str(templat->bind));

    struct mypipe_resource *mpr = CALLOC_STRUCT(mypipe_resource);
    if(!mpr)
        return NULL;

    assert(templat->format != PIPE_FORMAT_NONE);
    mpr->base = *templat;
    mpr->base.reference.count = 1;
    mpr->base.screen = screen;

    mpr->pot = (util_is_power_of_two_or_zero(templat->width0) &&
                util_is_power_of_two_or_zero(templat->height0) &&
                util_is_power_of_two_or_zero(templat->depth0));
    
    if(mpr->base.bind & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)){
        if(!mypipe_displaytarget_layout(screen, mpr, map_from_private)){
            FREE(mpr);
            return NULL;
        }
    }
    else {
        if(!mypipe_resource_layout(screen, mpr, true)){
            FREE(mpr);
            return NULL;
        }
    }

    return &mpr->base;
}

static struct pipe_resource * mypipe_resource_create(struct pipe_screen * screen,
                                                     const struct pipe_resource *templat){
    return mypipe_resource_create_front(screen, templat, NULL);
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
    struct mypipe_resource mpr;
    memset(&mpr, 0, sizeof(mpr));
    mpr.base = *res;
    return mypipe_resource_layout(screen, &mpr, false);
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
