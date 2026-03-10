#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void mypipe_set_shader_images(struct pipe_context *pipe,
                                     mesa_shader_stage shader,
                                     unsigned start_slot, unsigned count,
                                     unsigned unbind_num_trailing_slots,
                                     const struct pipe_image_view *images){
    fprintf(stderr, "STUB: mypipe_set_shader_images\n");
}

static void mypipe_set_shader_buffers(struct pipe_context *pipe,
                                      mesa_shader_stage shader,
                                      unsigned start_slot, unsigned count,
                                      const struct pipe_shader_buffer *buffers,
                                      unsigned writable_bitmask){
    fprintf(stderr, "STUB: mypipe_set_shader_buffers\n");
}

void mypipe_init_image_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_image_funcs\n");
    pipe->set_shader_images = mypipe_set_shader_images;
    pipe->set_shader_buffers = mypipe_set_shader_buffers;
}
