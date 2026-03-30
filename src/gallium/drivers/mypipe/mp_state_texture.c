#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"
#include "mp_texture.h"
#include "mp_screen.h"
#include "mp_context.h"
#include "mp_flush.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"
#include "util/u_blitter.h"
#include "util/format/u_format.h"

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
    struct mypipe_resource *mpr = mypipe_resource(resource);
    struct mypipe_transfer *mpt;
    struct pipe_transfer *pt;
    uint8_t *map;

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

    /* For buffers, box->x is the byte offset into the buffer */
    if (resource->target == PIPE_BUFFER)
        mpt->offset += box->x;

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
    struct mypipe_resource *mpr = mypipe_resource(resource);
    struct mypipe_transfer *mpt;
    struct pipe_transfer *pt;
    uint8_t *map;

    mpt = CALLOC_STRUCT(mypipe_transfer);
    if (!mpt)
        return NULL;

    pt = &mpt->base;
    pipe_resource_reference(&pt->resource, resource);
    pt->level = level;
    pt->usage = usage;
    pt->box = *box;
    pt->stride = mpr->stride[level];
    pt->layer_stride = mpr->img_stride[level];

    unsigned offset = mypipe_get_tex_image_offset(mpr, level, box->z);

    /* Add sub-offset for x,y within the image */
    enum pipe_format format = resource->format;
    unsigned bpp = util_format_get_blocksize(format);
    offset += box->y * pt->stride + box->x * bpp;

    mpt->offset = offset;

    /* Always use shadow buffer (mpr->data) for rendering and readback.
     * Display target is only written during flush_frontbuffer. */
    map = mpr->data;

    if (!map) {
        pipe_resource_reference(&pt->resource, NULL);
        FREE(mpt);
        return NULL;
    }
    *out_transfer = pt;
    return map + mpt->offset;
}

static void mypipe_texture_unmap(struct pipe_context *pipe,
                                 struct pipe_transfer *transfer){
    struct mypipe_resource *mpr;

    assert(transfer->resource);
    mpr = mypipe_resource(transfer->resource);

    if (transfer->usage & PIPE_MAP_WRITE)
        mpr->timestamp++;

    pipe_resource_reference(&transfer->resource, NULL);
    FREE(transfer);
}

static void mypipe_transfer_flush_region(struct pipe_context *pipe,
                                         struct pipe_transfer *transfer,
                                         const struct pipe_box *box){
}

static void mypipe_buffer_subdata(struct pipe_context *pipe,
                                  struct pipe_resource *resource,
                                  unsigned usage,
                                  unsigned offset,
                                  unsigned size,
                                  const void *data){
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
    struct pipe_transfer *transfer = NULL;

    usage |= PIPE_MAP_WRITE;

    uint8_t *map = pipe->texture_map(pipe, resource, level, usage, box, &transfer);
    if (!map)
        return;

    util_copy_box(map, resource->format,
                  transfer->stride, transfer->layer_stride,
                  0, 0, 0,
                  box->width, box->height, box->depth,
                  data, stride, layer_stride, 0, 0, 0);

    pipe->texture_unmap(pipe, transfer);
}

static struct pipe_surface *mypipe_create_surface(struct pipe_context *pipe,
                                                  struct pipe_resource *pt,
                                                  const struct pipe_surface *surf_templ){
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
    }
    return ps;
}

static void mypipe_surface_destroy(struct pipe_context *pipe,
                                   struct pipe_surface *surface){
    assert(surface->texture);
    pipe_resource_reference(&surface->texture, NULL);
    FREE(surface);
}

static void mypipe_clear_texture(struct pipe_context *pipe,
                                 struct pipe_resource *res,
                                 unsigned level,
                                 const struct pipe_box *box,
                                 const void *data){
}

static void mypipe_blit(struct pipe_context *pipe,
                        const struct pipe_blit_info *info){
    struct mypipe_context *ctx = mypipe_context(pipe);

    if (util_try_blit_via_copy_region(pipe, info, false))
        return;

    if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
        fprintf(stderr, "mypipe: blit unsupported %s -> %s\n",
                util_format_short_name(info->src.resource->format),
                util_format_short_name(info->dst.resource->format));
        return;
    }

    util_blitter_save_vertex_buffers(ctx->blitter, ctx->vertex_buffers,
                                     ctx->num_vertex_buffers);
    util_blitter_save_vertex_elements(ctx->blitter, ctx->velems);
    util_blitter_save_vertex_shader(ctx->blitter, ctx->vs);
    util_blitter_save_rasterizer(ctx->blitter, ctx->rasterizer);
    util_blitter_save_viewport(ctx->blitter, &ctx->viewport);
    util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
    util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
    util_blitter_save_blend(ctx->blitter, ctx->blend);
    util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->depth_stencil);
    util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
    util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer);
    util_blitter_save_fragment_sampler_states(ctx->blitter,
                      ctx->num_samplers[MESA_SHADER_FRAGMENT],
                      (void**)ctx->samplers[MESA_SHADER_FRAGMENT]);
    util_blitter_save_fragment_sampler_views(ctx->blitter,
                      ctx->num_sampler_views[MESA_SHADER_FRAGMENT],
                      ctx->sampler_views[MESA_SHADER_FRAGMENT]);
    util_blitter_blit(ctx->blitter, info, NULL);
}

void mypipe_init_context_texture_funcs(struct pipe_context *pipe){
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
    pipe->resource_copy_region = util_resource_copy_region;
    pipe->blit = mypipe_blit;
    pipe->resource_release = u_default_resource_release;
}
