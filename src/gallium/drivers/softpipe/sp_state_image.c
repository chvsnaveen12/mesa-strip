/*
 * Copyright 2016 Red Hat.
 * SPDX-License-Identifier: MIT
 */

#include "sp_context.h"
#include "sp_state.h"

static void softpipe_set_shader_images(struct pipe_context *pipe,
                                       mesa_shader_stage shader,
                                       unsigned start,
                                       unsigned num,
                                       unsigned unbind_num_trailing_slots,
                                       const struct pipe_image_view *images)
{
   /* images not supported in stripped build */
}

static void softpipe_set_shader_buffers(struct pipe_context *pipe,
                                        mesa_shader_stage shader,
                                        unsigned start,
                                        unsigned num,
                                        const struct pipe_shader_buffer *buffers,
                                        unsigned writable_bitmask)
{
   /* shader buffers not supported in stripped build */
}

void softpipe_init_image_funcs(struct pipe_context *pipe)
{
   pipe->set_shader_images = softpipe_set_shader_images;
   pipe->set_shader_buffers = softpipe_set_shader_buffers;
}
