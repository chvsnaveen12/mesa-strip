#include <stdio.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "mp_state.h"
#include "mp_context.h"

static int mp_type_size(const struct glsl_type *type, bool bindless) {
    return glsl_count_attribute_slots(type, false);
}

static void mp_lower_and_compile(struct mp_compiled_shader *shader) {
    nir_shader *nir = shader->nir;
    if (!nir) return;

    /* Lower deref-based I/O to intrinsic-based I/O (load_input/store_output) */
    nir_lower_io(nir, nir_var_shader_in | nir_var_shader_out,
                 mp_type_size, 0);

    /* Lower uniform derefs to load_uniform intrinsics */
    nir_lower_uniforms_to_ubo(nir, false, false);

    /* Scalar ALU */
    nir_lower_alu_to_scalar(nir, NULL, NULL);

    /* Optimize */
    nir_opt_constant_folding(nir);
    nir_opt_dce(nir);

    /* Build register map: walk all blocks looking for decl_reg intrinsics */
    memset(shader->reg_map, -1, sizeof(shader->reg_map));
    shader->num_regs = 0;

    nir_foreach_function_impl(impl, nir) {
        nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
                if (instr->type != nir_instr_type_intrinsic)
                    continue;
                nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
                if (intrin->intrinsic != nir_intrinsic_decl_reg)
                    continue;
                unsigned idx = intrin->def.index;
                if (idx < 128 && shader->reg_map[idx] < 0) {
                    shader->reg_map[idx] = shader->num_regs++;
                }
            }
        }
    }

    /* Compute FS input base offset: count system-value input slots
     * (POS, FACE) so we can adjust load_input base for user varyings */
    shader->fs_input_base_offset = 0;
    if (nir->info.stage == MESA_SHADER_FRAGMENT) {
        nir_foreach_function_impl(impl2, nir) {
            nir_foreach_block(block2, impl2) {
                nir_foreach_instr(instr2, block2) {
                    if (instr2->type != nir_instr_type_intrinsic) continue;
                    nir_intrinsic_instr *si = nir_instr_as_intrinsic(instr2);
                    if (si->intrinsic != nir_intrinsic_load_input) continue;
                    unsigned loc = nir_intrinsic_io_semantics(si).location;
                    if (loc == VARYING_SLOT_POS || loc == VARYING_SLOT_FACE) {
                        unsigned base = nir_intrinsic_base(si) + 1;
                        if (base > shader->fs_input_base_offset)
                            shader->fs_input_base_offset = base;
                    }
                }
            }
        }
    }

    fprintf(stderr, "  mp_lower_and_compile: %u hw regs allocated\n", shader->num_regs);
}

static void *mypipe_create_fs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    fprintf(stderr, "=== mypipe_create_fs_state ===\n");
    struct mp_compiled_shader *shader = CALLOC_STRUCT(mp_compiled_shader);
    if (templ->type == PIPE_SHADER_IR_NIR && templ->ir.nir) {
        shader->nir = templ->ir.nir;
        mp_lower_and_compile(shader);
    } else {
        fprintf(stderr, "  IR type: %d (not NIR)\n", templ->type);
    }
    return (void *)shader;
}

static void mypipe_bind_fs_state(struct pipe_context *pipe, void *fs){
    mypipe_context(pipe)->fs = fs;
}

static void mypipe_delete_fs_state(struct pipe_context *pipe, void *fs){
    struct mp_compiled_shader *shader = fs;
    /* BUG FIX: was ralloc_free(fs) — double free; correct: free only the NIR */
    if(shader->nir)
        ralloc_free(shader->nir);
    FREE(shader);
}

static void *mypipe_create_vs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    fprintf(stderr, "=== mypipe_create_vs_state ===\n");
    struct mp_compiled_shader *shader = CALLOC_STRUCT(mp_compiled_shader);
    if (templ->type == PIPE_SHADER_IR_NIR && templ->ir.nir) {
        shader->nir = templ->ir.nir;
        mp_lower_and_compile(shader);
    } else {
        fprintf(stderr, "  IR type: %d (not NIR)\n", templ->type);
    }
    return (void *)shader;
}

static void mypipe_bind_vs_state(struct pipe_context *pipe, void *vs){
    mypipe_context(pipe)->vs = vs;
}

static void mypipe_delete_vs_state(struct pipe_context *pipe, void *vs){
    struct mp_compiled_shader *shader = vs;
    /* BUG FIX: was ralloc_free(vs) — double free; correct: free only the NIR */
    if(shader->nir)
        ralloc_free(shader->nir);
    FREE(shader);
}

static void *mypipe_create_gs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ){
    return NULL;
}

static void mypipe_bind_gs_state(struct pipe_context *pipe, void *gs){
}

static void mypipe_delete_gs_state(struct pipe_context *pipe, void *gs){
}

static void mypipe_set_constant_buffer(struct pipe_context *pipe,
                                       mesa_shader_stage shader, uint index,
                                       const struct pipe_constant_buffer *buf){
    struct mypipe_context *ctx = mypipe_context(pipe);

    if (index != 0) return; /* only support cbuf 0 */

    struct pipe_constant_buffer *dst;
    if (shader == MESA_SHADER_VERTEX)
        dst = &ctx->vs_cbuf;
    else if (shader == MESA_SHADER_FRAGMENT)
        dst = &ctx->fs_cbuf;
    else
        return;

    if (buf) {
        pipe_resource_reference(&dst->buffer, buf->buffer);
        dst->buffer_offset = buf->buffer_offset;
        dst->buffer_size = buf->buffer_size;
        dst->user_buffer = buf->user_buffer;
    } else {
        pipe_resource_reference(&dst->buffer, NULL);
        dst->user_buffer = NULL;
        dst->buffer_offset = 0;
        dst->buffer_size = 0;
    }
}

void mypipe_init_shader_funcs(struct pipe_context *pipe){
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
