#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"
#include "mp_texture.h"
#include "mp_screen.h"
#include "mp_flush.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"


#include "frontend/sw_winsys.h"

unsigned mypipe_get_tex_image_offset(const struct mypipe_resource *mpr, unsigned level, unsigned layer){
    unsigned offset = mpr->level_offset[level];
    offset += layer * mpr->img_stride[level];
    return offset;
}

static void *mypipe_transfer_map(struct pipe_context *pipe,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **transfer){
    fprintf(stderr, "STUB: mypipe_buffer_map\n");
    struct sw_winsys *winsys = mypipe_screen(pipe->screen)->winsys;
    struct mypipe_resource *mpr = mypipe_resource(resource);
    struct mypipe_transfer *mpt;
    struct pipe_transfer *pt;
    enum pipe_format format = resource->format;
    uint8_t *map;

    // FIXME: A BUNCH OF ASSERTS

    // What's even the point of this??!!?!?!
    if(!(usage & PIPE_MAP_UNSYNCHRONIZED)){
        bool read_only = !(usage & PIPE_MAP_WRITE);
        bool do_not_block = !!(usage & PIPE_MAP_DONTBLOCK);
        if(!mypipe_flush_resource(pipe, resource, level, box->depth >1 ? -1 : box->z, 0, read_only, true, do_not_block)){
            assert(do_not_block);
            return NULL;
        }
    }
    
    mpt = CALLOC_STRUCT(mypipe_transfer);

    if(!mpt)
        return NULL;

    pt = &mpt->base;
    pipe_resource_reference(&pt->resource, resource);
    pt->level = level;
    pt->usage = usage;
    pt->box = *box;
    pt->stride = mpr->stride[level];
    pt->layer_stride = mpr->img_stride[level];

    mpt->offset = mypipe_get_tex_image_offset(mpr, level, box->z);

    if(mpr->dt)
        map = winsys->displaytarget_map(winsys, mpr->dt, usage);
    else
        map = mpr->data;
    
    if(!map){
        pipe_resource_reference(&pt->resource, NULL);
        FREE(mpt);
        return NULL;
    }
    *transfer = pt;
    return map + mpt->offset;
}

static void mypipe_transfer_unmap(struct pipe_context *pipe,
                                struct pipe_transfer *transfer){
    fprintf(stderr, "STUB: mypipe_buffer_unmap\n");

    struct mypipe_resource *mpr;

    assert(transfer->resource);
    mpr = mypipe_resource(transfer->resource);

    if(mpr->dt){
        struct sw_winsys *winsys = mypipe_screen(pipe->screen)->winsys;
        winsys->displaytarget_unmap(winsys, mpr->dt);
    }

    if (transfer->usage & PIPE_MAP_WRITE){
        mpr->timestamp++;
    }
    pipe_resource_reference(&transfer->resource, NULL);
    FREE(transfer);
}

static void *mypipe_texture_map(struct pipe_context *pipe,
                                struct pipe_resource *resource,
                                unsigned level,
                                unsigned usage,
                                const struct pipe_box *box,
                                struct pipe_transfer **out_transfer){
    fprintf(stderr, "STUB: mypipe_texture_map\n");
    return NULL;
}

static void mypipe_texture_unmap(struct pipe_context *pipe,
                                 struct pipe_transfer *transfer){
    fprintf(stderr, "STUB: mypipe_texture_unmap\n");
}

static void mypipe_transfer_flush_region(struct pipe_context *pipe,
                                         struct pipe_transfer *transfer,
                                         const struct pipe_box *box){
    fprintf(stderr, "STUB: mypipe_transfer_flush_region\n");
}

static void mypipe_buffer_subdata(struct pipe_context *pipe,
                                  struct pipe_resource *resource,
                                  unsigned usage,
                                  unsigned offset,
                                  unsigned size,
                                  const void *data){
    fprintf(stderr, "STUB: mypipe_buffer_subdata: offset=%u size=%u data=%p resource_width=%u\n",
            offset, size, data, resource->width0);

    struct pipe_transfer *transfer = NULL;
    struct pipe_box box;

    assert(!(usage & PIPE_MAP_READ));
    usage |= PIPE_MAP_WRITE;

    if(!(usage & PIPE_MAP_DIRECTLY)){
        if(offset == 0 && size == resource->width0){
            usage |= PIPE_MAP_DISCARD_WHOLE_RESOURCE;
        }
        else    
            usage |= PIPE_MAP_DISCARD_RANGE;
    }

    u_box_1d(offset, size, & box);

    uint8_t *map = pipe->buffer_map(pipe, resource, 0, usage, &box, &transfer);
    if(!map)
        return;

    memcpy(map, data, size);
    pipe_buffer_unmap(pipe, transfer);
}

static void mypipe_texture_subdata(struct pipe_context *pipe,
                                   struct pipe_resource *resource,
                                   unsigned level,
                                   unsigned usage,
                                   const struct pipe_box *box,
                                   const void *data,
                                   unsigned stride,
                                   uintptr_t layer_stride){
    fprintf(stderr, "STUB: mypipe_texture_subdata\n");
}

static struct pipe_surface *mypipe_create_surface(struct pipe_context *pipe,
                                                  struct pipe_resource *pt,
                                                  const struct pipe_surface *surf_templ){
    fprintf(stderr, "STUB: mypipe_create_surface\n");

    struct pipe_surface *ps;

    ps = CALLOC_STRUCT(pipe_surface);

    if(ps){
        pipe_reference_init(&ps->reference, 1);
        pipe_resource_reference(&ps->texture, pt);

        ps->context = pipe;
        ps->format = surf_templ->format;
        assert(surf_templ->level <= pt->last_level);
        ps->level = surf_templ->level;
        ps->first_layer = surf_templ->first_layer;
        ps->last_layer = surf_templ->last_layer;
        if(ps->first_layer != ps->last_layer)
            fprintf(stderr, "mypipe_create_surface: creating surface with multiple layers, rendering to first layer only\n");
    }
    return ps;
}

static void mypipe_surface_destroy(struct pipe_context *pipe,
                                   struct pipe_surface *surface){
    fprintf(stderr, "STUB: mypipe_surface_destroy\n");
    assert(surface->texture);
    pipe_resource_reference(&surface->texture, NULL);
    FREE(surface);
}

static void mypipe_clear_texture(struct pipe_context *pipe,
                                 struct pipe_resource *res,
                                 unsigned level,
                                 const struct pipe_box *box,
                                 const void *data){
    fprintf(stderr, "STUB: mypipe_clear_texture\n");
}

void mypipe_init_context_texture_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_context_texture_funcs\n");
    pipe->buffer_map = mypipe_transfer_map;
    pipe->buffer_unmap = mypipe_transfer_unmap;
    pipe->texture_map = mypipe_texture_map;
    pipe->texture_unmap = mypipe_texture_unmap;
    pipe->transfer_flush_region = mypipe_transfer_flush_region;
    pipe->buffer_subdata = mypipe_buffer_subdata;
    pipe->texture_subdata = mypipe_texture_subdata;
    pipe->create_surface = mypipe_create_surface;
    pipe->surface_destroy = mypipe_surface_destroy;
    pipe->clear_texture = mypipe_clear_texture;
}
