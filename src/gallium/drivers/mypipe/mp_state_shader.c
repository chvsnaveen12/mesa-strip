#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "mp_state.h"

static void *mypipe_create_fs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    fprintf(stderr, "STUB: mypipe_create_fs_state\n");
    return NULL;
}

static void mypipe_bind_fs_state(struct pipe_context *pipe, void *fs){
    fprintf(stderr, "STUB: mypipe_bind_fs_state\n");
}

static void mypipe_delete_fs_state(struct pipe_context *pipe, void *fs){
    fprintf(stderr, "STUB: mypipe_delete_fs_state\n");
}

static void *mypipe_create_vs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    fprintf(stderr, "STUB: mypipe_create_vs_state\n");
    return NULL;
}

static void mypipe_bind_vs_state(struct pipe_context *pipe, void *vs){
    fprintf(stderr, "STUB: mypipe_bind_vs_state\n");
}

static void mypipe_delete_vs_state(struct pipe_context *pipe, void *vs){
    fprintf(stderr, "STUB: mypipe_delete_vs_state\n");
}

static void *mypipe_create_gs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    fprintf(stderr, "STUB: mypipe_create_gs_state\n");
    return NULL;
}

static void mypipe_bind_gs_state(struct pipe_context *pipe, void *gs){
    fprintf(stderr, "STUB: mypipe_bind_gs_state\n");
}

static void mypipe_delete_gs_state(struct pipe_context *pipe, void *gs){
    fprintf(stderr, "STUB: mypipe_delete_gs_state\n");
}

static void mypipe_set_constant_buffer(struct pipe_context *pipe,
                                       mesa_shader_stage shader, uint index,
                                       const struct pipe_constant_buffer *buf){
    fprintf(stderr, "STUB: mypipe_set_constant_buffer\n");
}

void mypipe_init_shader_funcs(struct pipe_context *pipe){
    fprintf(stderr, "STUB: mypipe_init_shader_funcs\n");
    pipe->create_fs_state = mypipe_create_fs_state;
    pipe->bind_fs_state = mypipe_bind_fs_state;
    pipe->delete_fs_state = mypipe_delete_fs_state;
    pipe->create_vs_state = mypipe_create_vs_state;
    pipe->bind_vs_state = mypipe_bind_vs_state;
    pipe->delete_vs_state = mypipe_delete_vs_state;
    pipe->create_gs_state = mypipe_create_gs_state;
    pipe->bind_gs_state = mypipe_bind_gs_state;
    pipe->delete_gs_state = mypipe_delete_gs_state;
    pipe->set_constant_buffer = mypipe_set_constant_buffer;
}
