#ifndef MP_CONTEXT_H
#define MP_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "nir/nir.h"

#define MP_MAX_ATTRIBS  16
#define MP_MAX_SAMPLERS  8

struct mp_vertex_element_state {
    unsigned num_elements;
    struct pipe_vertex_element elements[MP_MAX_ATTRIBS];
};

struct mp_compiled_shader {
    nir_shader *nir;
    unsigned num_regs;        /* hw register count after out-of-SSA */
    int reg_map[128];         /* decl_reg def.index -> compact hw reg ID */
    unsigned fs_input_base_offset; /* system-value input slots (POS, FACE)
                                    * preceding user varyings in nir_lower_io
                                    * base numbering */
};

struct mypipe_context {
    struct pipe_context pipe;
    struct pipe_framebuffer_state framebuffer;

    /* Vertex pipeline */
    struct pipe_viewport_state viewport;
    struct pipe_vertex_buffer vertex_buffers[MP_MAX_ATTRIBS];
    unsigned num_vertex_buffers;
    struct mp_vertex_element_state *velems;

    /* Shaders */
    struct mp_compiled_shader *vs;
    struct mp_compiled_shader *fs;

    /* Fixed-function state (Phase 1) */
    struct pipe_rasterizer_state *rasterizer;
    struct pipe_depth_stencil_alpha_state *depth_stencil;
    struct pipe_blend_state *blend;
    struct pipe_scissor_state scissor;
    struct pipe_stencil_ref stencil_ref;
    struct pipe_blend_color blend_color;

    /* Constant buffers (Phase 2) */
    struct pipe_constant_buffer vs_cbuf;
    struct pipe_constant_buffer fs_cbuf;

    /* Texture sampling (Phase 4) */
    struct pipe_sampler_view *sampler_views[MESA_SHADER_STAGES][MP_MAX_SAMPLERS];
    unsigned num_sampler_views[MESA_SHADER_STAGES];
    struct pipe_sampler_state *samplers[MESA_SHADER_STAGES][MP_MAX_SAMPLERS];
    unsigned num_samplers[MESA_SHADER_STAGES];
};

static inline struct mypipe_context * mypipe_context(struct pipe_context * pipe){
    return (struct mypipe_context*)pipe;
}

struct pipe_context *mypipe_create_context(struct pipe_screen * , void *priv, unsigned int flags);

/* Compile a NIR shader for the mypipe interpreter */
void mp_lower_and_compile(struct mp_compiled_shader *shader);

#endif
