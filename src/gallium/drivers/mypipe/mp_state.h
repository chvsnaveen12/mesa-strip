#ifndef MP_STATE_H
#define MP_STATE_H

struct pipe_context;

void mypipe_init_blend_funcs(struct pipe_context *pipe);
void mypipe_init_clip_funcs(struct pipe_context *pipe);
void mypipe_init_rasterizer_funcs(struct pipe_context *pipe);
void mypipe_init_sampler_funcs(struct pipe_context *pipe);
void mypipe_init_shader_funcs(struct pipe_context *pipe);
void mypipe_init_streamout_funcs(struct pipe_context *pipe);
void mypipe_init_context_texture_funcs(struct pipe_context *pipe);
void mypipe_init_vertex_funcs(struct pipe_context *pipe);
void mypipe_init_image_funcs(struct pipe_context *pipe);
void mypipe_init_query_funcs(struct pipe_context *pipe);

#endif
