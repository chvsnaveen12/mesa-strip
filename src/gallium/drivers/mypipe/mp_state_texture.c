#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_buffer_map(struct pipe_context *pipe,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **out_transfer){
    fprintf(stderr, "STUB: mypipe_buffer_map\n");
    return NULL;
}

static void mypipe_buffer_unmap(struct pipe_context *pipe,
                                struct pipe_transfer *transfer){
    fprintf(stderr, "STUB: mypipe_buffer_unmap\n");
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
    fprintf(stderr, "STUB: mypipe_buffer_subdata\n");
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
                                                  struct pipe_resource *resource,
                                                  const struct pipe_surface *templ){
    fprintf(stderr, "STUB: mypipe_create_surface\n");
    return NULL;
}

static void mypipe_surface_destroy(struct pipe_context *pipe,
                                   struct pipe_surface *surface){
    fprintf(stderr, "STUB: mypipe_surface_destroy\n");
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
    pipe->buffer_map = mypipe_buffer_map;
    pipe->buffer_unmap = mypipe_buffer_unmap;
    pipe->texture_map = mypipe_texture_map;
    pipe->texture_unmap = mypipe_texture_unmap;
    pipe->transfer_flush_region = mypipe_transfer_flush_region;
    pipe->buffer_subdata = mypipe_buffer_subdata;
    pipe->texture_subdata = mypipe_texture_subdata;
    pipe->create_surface = mypipe_create_surface;
    pipe->surface_destroy = mypipe_surface_destroy;
    pipe->clear_texture = mypipe_clear_texture;
}
