#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "mp_shader_exec.h"
#include "mp_context.h"
#include "mp_texture.h"
#include "mp_screen.h"
#include "compiler/nir/nir.h"
#include "util/format/u_format.h"
#include "frontend/sw_winsys.h"

/* ------------------------------------------------------------------ */
/* Texture sampling                                                    */
/* ------------------------------------------------------------------ */

static float apply_wrap(float coord, unsigned wrap_mode, unsigned size) {
    switch (wrap_mode) {
    case PIPE_TEX_WRAP_REPEAT: {
        float f = coord - floorf(coord);
        return f * (float)size;
    }
    case PIPE_TEX_WRAP_CLAMP_TO_EDGE: {
        float f = CLAMP(coord, 0.0f, 1.0f);
        return f * (float)size - 0.5f;
    }
    case PIPE_TEX_WRAP_CLAMP: {
        float f = CLAMP(coord, 0.0f, 1.0f);
        return f * (float)size;
    }
    case PIPE_TEX_WRAP_MIRROR_REPEAT: {
        float f = coord - floorf(coord);
        int integer = (int)floorf(coord);
        if (integer & 1)
            f = 1.0f - f;
        return f * (float)size;
    }
    default:
        return coord * (float)size;
    }
}

static void fetch_texel(const uint8_t *data, unsigned stride,
                        enum pipe_format format,
                        int x, int y, unsigned w, unsigned h,
                        float out[4]) {
    /* Clamp to valid range */
    x = CLAMP(x, 0, (int)w - 1);
    y = CLAMP(y, 0, (int)h - 1);

    unsigned bpp = util_format_get_blocksize(format);
    const uint8_t *texel = data + y * stride + x * bpp;
    float rgba[4] = {0, 0, 0, 1};
    util_format_unpack_rgba(format, rgba, texel, 1);
    out[0] = rgba[0];
    out[1] = rgba[1];
    out[2] = rgba[2];
    out[3] = rgba[3];
}

static void sample_texture_2d(const struct mypipe_context *ctx,
                               unsigned sampler_idx,
                               mesa_shader_stage stage,
                               float s, float t,
                               float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;

    if (sampler_idx >= MP_MAX_SAMPLERS) return;

    struct pipe_sampler_view *view = ctx->sampler_views[stage][sampler_idx];
    struct pipe_sampler_state *samp = ctx->samplers[stage][sampler_idx];
    if (!view || !samp || !view->texture) return;

    struct mypipe_resource *mpr = mypipe_resource(view->texture);
    unsigned w = view->texture->width0;
    unsigned h = view->texture->height0;
    enum pipe_format format = view->format;

    uint8_t *data;
    if (mpr->dt) {
        struct sw_winsys *winsys = mypipe_screen(view->texture->screen)->winsys;
        data = winsys->displaytarget_map(winsys, mpr->dt, PIPE_MAP_READ);
    } else {
        data = mpr->data;
    }
    if (!data) return;

    unsigned stride = mpr->stride[0];

    float fx = apply_wrap(s, samp->wrap_s, w);
    float fy = apply_wrap(t, samp->wrap_t, h);

    if (samp->min_img_filter == PIPE_TEX_FILTER_LINEAR ||
        samp->mag_img_filter == PIPE_TEX_FILTER_LINEAR) {
        /* Bilinear filtering */
        float x0f = fx - 0.5f;
        float y0f = fy - 0.5f;
        int x0 = (int)floorf(x0f);
        int y0 = (int)floorf(y0f);
        float xfrac = x0f - (float)x0;
        float yfrac = y0f - (float)y0;

        float t00[4], t10[4], t01[4], t11[4];
        fetch_texel(data, stride, format, x0,     y0,     w, h, t00);
        fetch_texel(data, stride, format, x0 + 1, y0,     w, h, t10);
        fetch_texel(data, stride, format, x0,     y0 + 1, w, h, t01);
        fetch_texel(data, stride, format, x0 + 1, y0 + 1, w, h, t11);

        for (int c = 0; c < 4; c++) {
            float top    = t00[c] + (t10[c] - t00[c]) * xfrac;
            float bottom = t01[c] + (t11[c] - t01[c]) * xfrac;
            out[c] = top + (bottom - top) * yfrac;
        }
    } else {
        /* Nearest filtering */
        int ix = (int)floorf(fx);
        int iy = (int)floorf(fy);
        fetch_texel(data, stride, format, ix, iy, w, h, out);
    }

    /* Apply swizzle from sampler view */
    float tmp[4];
    memcpy(tmp, out, sizeof(tmp));
    unsigned swizzles[4] = { view->swizzle_r, view->swizzle_g, view->swizzle_b, view->swizzle_a };
    for (int c = 0; c < 4; c++) {
        switch (swizzles[c]) {
        case PIPE_SWIZZLE_X: out[c] = tmp[0]; break;
        case PIPE_SWIZZLE_Y: out[c] = tmp[1]; break;
        case PIPE_SWIZZLE_Z: out[c] = tmp[2]; break;
        case PIPE_SWIZZLE_W: out[c] = tmp[3]; break;
        case PIPE_SWIZZLE_0: out[c] = 0.0f; break;
        case PIPE_SWIZZLE_1: out[c] = 1.0f; break;
        default: break;
        }
    }

    if (mpr->dt) {
        struct sw_winsys *winsys = mypipe_screen(view->texture->screen)->winsys;
        winsys->displaytarget_unmap(winsys, mpr->dt);
    }
}

/* ------------------------------------------------------------------ */
/* Read an NIR src value                                               */
/* ------------------------------------------------------------------ */

static void read_src(const nir_src *src, const float t[][4],
                     const float hw[][4], const struct mp_compiled_shader *shader,
                     float out[4]) {
    unsigned idx = src->ssa->index;
    if (idx < MP_MAX_SSA) {
        /* BUG FIX: use t (SSA temp array), not regs */
        memcpy(out, t[idx], 4 * sizeof(float));
    } else {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
    }
}

static void write_dest(const nir_def *def, float t[][4], const float val[4]) {
    unsigned idx = def->index;
    if (idx < MP_MAX_SSA) {
        for (unsigned c = 0; c < def->num_components; c++)
            t[idx][c] = val[c];
    }
}

/* ------------------------------------------------------------------ */
/* Core NIR interpreter                                                */
/* ------------------------------------------------------------------ */

static void mp_exec_shader(const struct mp_compiled_shader *shader,
                            const struct mypipe_context *ctx,
                            mesa_shader_stage stage,
                            float inputs[][4], unsigned num_inputs,
                            float outputs[][4], unsigned *num_outputs,
                            const void *uniforms,
                            float frag_coord[4],
                            bool front_face,
                            bool *discard_flag) {
    nir_shader *nir = shader->nir;
    if (!nir) return;

    float t[MP_MAX_SSA][4];
    float hw[MP_MAX_HW_REGS][4];
    memset(t, 0, sizeof(t));
    memset(hw, 0, sizeof(hw));

    *num_outputs = 0;

    nir_foreach_function_impl(impl, nir) {
        nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
                switch (instr->type) {

                case nir_instr_type_load_const: {
                    nir_load_const_instr *lc = nir_instr_as_load_const(instr);
                    unsigned idx = lc->def.index;
                    if (idx < MP_MAX_SSA) {
                        for (unsigned c = 0; c < lc->def.num_components; c++)
                            t[idx][c] = lc->value[c].f32;
                    }
                    break;
                }

                case nir_instr_type_alu: {
                    nir_alu_instr *alu = nir_instr_as_alu(instr);
                    float src[4][4];

                    for (unsigned s = 0; s < nir_op_infos[alu->op].num_inputs; s++) {
                        float raw[4];
                        read_src(&alu->src[s].src, (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, raw);
                        /* Apply swizzle */
                        for (unsigned c = 0; c < 4; c++)
                            src[s][c] = raw[alu->src[s].swizzle[c]];
                    }

                    float dst[4] = {0, 0, 0, 0};
                    unsigned nc = alu->def.num_components;

                    for (unsigned c = 0; c < nc; c++) {
                        float a = src[0][c];
                        float b = (nir_op_infos[alu->op].num_inputs > 1) ? src[1][c] : 0;
                        float cc_ = (nir_op_infos[alu->op].num_inputs > 2) ? src[2][c] : 0;
                        mp_reg ra, rb, rc;
                        ra.f = a; rb.f = b; rc.f = cc_;

                        switch (alu->op) {
                        case nir_op_mov:     dst[c] = a; break;
                        case nir_op_fneg:    dst[c] = -a; break;
                        case nir_op_fabs:    dst[c] = fabsf(a); break;
                        case nir_op_fsat:    dst[c] = CLAMP(a, 0.0f, 1.0f); break;
                        case nir_op_fsign:   dst[c] = (a > 0) ? 1.0f : (a < 0) ? -1.0f : 0.0f; break;
                        case nir_op_frcp:    dst[c] = (a != 0.0f) ? 1.0f / a : 0.0f; break;
                        case nir_op_frsq:    dst[c] = (a > 0.0f) ? 1.0f / sqrtf(a) : 0.0f; break;
                        case nir_op_fsqrt:   dst[c] = sqrtf(fabsf(a)); break;
                        case nir_op_fexp2:   dst[c] = exp2f(a); break;
                        case nir_op_flog2:   dst[c] = (a > 0.0f) ? log2f(a) : -FLT_MAX; break;
                        case nir_op_fsin:    dst[c] = sinf(a); break;
                        case nir_op_fcos:    dst[c] = cosf(a); break;
                        case nir_op_ffloor:  dst[c] = floorf(a); break;
                        case nir_op_fceil:   dst[c] = ceilf(a); break;
                        case nir_op_ffract:  dst[c] = a - floorf(a); break;
                        case nir_op_fround_even: dst[c] = roundf(a); break;
                        case nir_op_fadd:    dst[c] = a + b; break;
                        case nir_op_fsub:    dst[c] = a - b; break;
                        case nir_op_fmul:    dst[c] = a * b; break;
                        case nir_op_fdiv:    dst[c] = (b != 0.0f) ? a / b : 0.0f; break;
                        case nir_op_fmin:    dst[c] = fminf(a, b); break;
                        case nir_op_fmax:    dst[c] = fmaxf(a, b); break;
                        case nir_op_fpow:    dst[c] = powf(a, b); break;
                        case nir_op_fmod:    dst[c] = a - b * floorf(a / b); break;
                        case nir_op_ffma:    dst[c] = a * b + cc_; break;
                        case nir_op_flrp:    dst[c] = a * (1.0f - cc_) + b * cc_; break;
                        case nir_op_iadd:    { mp_reg r; r.i = ra.i + rb.i; dst[c] = r.f; break; }
                        case nir_op_isub:    { mp_reg r; r.i = ra.i - rb.i; dst[c] = r.f; break; }
                        case nir_op_imul:    { mp_reg r; r.i = ra.i * rb.i; dst[c] = r.f; break; }
                        case nir_op_ineg:    { mp_reg r; r.i = -ra.i; dst[c] = r.f; break; }
                        case nir_op_iabs:    { mp_reg r; r.i = (ra.i < 0) ? -ra.i : ra.i; dst[c] = r.f; break; }
                        case nir_op_feq:
                        case nir_op_feq32:   { mp_reg r; r.u = (a == b) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_fneu:
                        case nir_op_fneu32:  { mp_reg r; r.u = (a != b) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_flt:
                        case nir_op_flt32:   { mp_reg r; r.u = (a < b) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_fge:
                        case nir_op_fge32:   { mp_reg r; r.u = (a >= b) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_ieq:
                        case nir_op_ieq32:   { mp_reg r; r.u = (ra.i == rb.i) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_ine:
                        case nir_op_ine32:   { mp_reg r; r.u = (ra.i != rb.i) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_ilt:
                        case nir_op_ilt32:   { mp_reg r; r.u = (ra.i < rb.i) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_ige:
                        case nir_op_ige32:   { mp_reg r; r.u = (ra.i >= rb.i) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_ult:
                        case nir_op_ult32:   { mp_reg r; r.u = (ra.u < rb.u) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_uge:
                        case nir_op_uge32:   { mp_reg r; r.u = (ra.u >= rb.u) ? ~0u : 0u; dst[c] = r.f; break; }
                        case nir_op_iand:    { mp_reg r; r.u = ra.u & rb.u; dst[c] = r.f; break; }
                        case nir_op_ior:     { mp_reg r; r.u = ra.u | rb.u; dst[c] = r.f; break; }
                        case nir_op_inot:    { mp_reg r; r.u = ~ra.u; dst[c] = r.f; break; }
                        case nir_op_ixor:    { mp_reg r; r.u = ra.u ^ rb.u; dst[c] = r.f; break; }
                        /* GLSL 1.10 float-result comparisons (return 0.0 or 1.0) */
                        case nir_op_slt:     dst[c] = (a < b) ? 1.0f : 0.0f; break;
                        case nir_op_sge:     dst[c] = (a >= b) ? 1.0f : 0.0f; break;
                        case nir_op_seq:     dst[c] = (a == b) ? 1.0f : 0.0f; break;
                        case nir_op_sne:     dst[c] = (a != b) ? 1.0f : 0.0f; break;
                        case nir_op_bcsel:   { mp_reg cond; cond.f = a; dst[c] = cond.u ? b : cc_; break; }
                        case nir_op_b2f32:   { mp_reg cond; cond.f = a; dst[c] = cond.u ? 1.0f : 0.0f; break; }
                        case nir_op_b2i32:   { mp_reg cond, r; cond.f = a; r.i = cond.u ? 1 : 0; dst[c] = r.f; break; }
                        case nir_op_f2i32:   { mp_reg r; r.i = (int32_t)a; dst[c] = r.f; break; }
                        case nir_op_i2f32:   { dst[c] = (float)ra.i; break; }
                        case nir_op_f2u32:   { mp_reg r; r.u = (uint32_t)a; dst[c] = r.f; break; }
                        case nir_op_u2f32:   { dst[c] = (float)ra.u; break; }
                        case nir_op_ishl:    { mp_reg r; r.i = ra.i << (rb.u & 31); dst[c] = r.f; break; }
                        case nir_op_ishr:    { mp_reg r; r.i = ra.i >> (rb.u & 31); dst[c] = r.f; break; }
                        case nir_op_ushr:    { mp_reg r; r.u = ra.u >> (rb.u & 31); dst[c] = r.f; break; }
                        case nir_op_umin:    { mp_reg r; r.u = (ra.u < rb.u) ? ra.u : rb.u; dst[c] = r.f; break; }
                        case nir_op_umax:    { mp_reg r; r.u = (ra.u > rb.u) ? ra.u : rb.u; dst[c] = r.f; break; }
                        case nir_op_imin:    { mp_reg r; r.i = (ra.i < rb.i) ? ra.i : rb.i; dst[c] = r.f; break; }
                        case nir_op_imax:    { mp_reg r; r.i = (ra.i > rb.i) ? ra.i : rb.i; dst[c] = r.f; break; }

                        /* Multi-component packing */
                        case nir_op_vec2:
                        case nir_op_vec3:
                        case nir_op_vec4:
                            /* For vec ops, each component comes from src[c][0] */
                            dst[c] = src[c][0];
                            break;

                        default:
                            fprintf(stderr, "mp_exec: unhandled ALU op %d\n", alu->op);
                            break;
                        }
                    }
                    write_dest(&alu->def, t, dst);
                    break;
                }

                case nir_instr_type_intrinsic: {
                    nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
                    switch (intrin->intrinsic) {

                    case nir_intrinsic_decl_reg:
                        /* Already processed during compilation */
                        break;

                    case nir_intrinsic_load_reg: {
                        nir_src *reg_src = &intrin->src[0];
                        unsigned def_idx = reg_src->ssa->index;
                        int hw_id = (def_idx < 128) ? shader->reg_map[def_idx] : -1;
                        float val[4] = {0, 0, 0, 0};
                        if (hw_id >= 0 && hw_id < MP_MAX_HW_REGS)
                            memcpy(val, hw[hw_id], 4 * sizeof(float));
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_store_reg: {
                        float val[4];
                        read_src(&intrin->src[0], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, val);
                        nir_src *reg_src = &intrin->src[1];
                        unsigned def_idx = reg_src->ssa->index;
                        int hw_id = (def_idx < 128) ? shader->reg_map[def_idx] : -1;
                        if (hw_id >= 0 && hw_id < MP_MAX_HW_REGS) {
                            unsigned wrmask = nir_intrinsic_write_mask(intrin);
                            for (unsigned c = 0; c < 4; c++) {
                                if (wrmask & (1 << c))
                                    hw[hw_id][c] = val[c];
                            }
                        }
                        break;
                    }

                    case nir_intrinsic_load_input: {
                        /* Use base (driver_location) — assigned sequentially by
                         * nir_lower_io, matches vertex element order for VS and
                         * varying order for FS. */
                        unsigned slot = nir_intrinsic_base(intrin);
                        unsigned comp = nir_intrinsic_component(intrin);
                        float val[4] = {0, 0, 0, 0};
                        if (slot < (unsigned)num_inputs) {
                            for (unsigned c = 0; c < intrin->def.num_components; c++)
                                val[c] = inputs[slot][comp + c];
                        }
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_store_output: {
                        float val[4];
                        read_src(&intrin->src[0], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, val);
                        unsigned loc = nir_intrinsic_io_semantics(intrin).location;
                        unsigned comp = nir_intrinsic_component(intrin);
                        unsigned slot;
                        if (stage == MESA_SHADER_VERTEX && loc == VARYING_SLOT_PSIZ) {
                            /* Point size goes to special slot */
                            slot = MP_MAX_VARYINGS + 1;
                        } else {
                            /* Use base (driver_location) for everything else */
                            slot = nir_intrinsic_base(intrin);
                        }
                        unsigned nc = nir_intrinsic_src_components(intrin, 0);
                        unsigned wrmask = nir_intrinsic_write_mask(intrin);
                        for (unsigned c = 0; c < nc; c++) {
                            if (wrmask & (1u << c))
                                outputs[slot][comp + c] = val[c];
                        }
                        if (slot >= *num_outputs)
                            *num_outputs = slot + 1;
                        break;
                    }

                    case nir_intrinsic_load_uniform: {
                        float val[4] = {0, 0, 0, 0};
                        if (uniforms) {
                            unsigned base = nir_intrinsic_base(intrin);
                            unsigned range = nir_intrinsic_range(intrin);
                            float offset_val[4];
                            read_src(&intrin->src[0], (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, offset_val);
                            mp_reg off_reg;
                            off_reg.f = offset_val[0];
                            unsigned byte_offset = base + off_reg.u;
                            unsigned nc = intrin->def.num_components;
                            const uint8_t *udata = (const uint8_t *)uniforms;
                            for (unsigned c = 0; c < nc && (byte_offset + c * 4) < range + base; c++) {
                                memcpy(&val[c], udata + byte_offset + c * 4, 4);
                            }
                        }
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_load_ubo: {
                        float val[4] = {0, 0, 0, 0};
                        /* src[0] = UBO index (0 = default), src[1] = byte offset */
                        float idx_val[4], off_val[4];
                        read_src(&intrin->src[0], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, idx_val);
                        read_src(&intrin->src[1], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, off_val);
                        mp_reg ubo_idx, byte_off;
                        ubo_idx.f = idx_val[0];
                        byte_off.f = off_val[0];
                        if (ubo_idx.u == 0 && uniforms) {
                            unsigned nc = intrin->def.num_components;
                            const uint8_t *udata = (const uint8_t *)uniforms;
                            for (unsigned c = 0; c < nc; c++)
                                memcpy(&val[c], udata + byte_off.u + c * 4, 4);
                        }
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_load_frag_coord: {
                        float val[4] = {0, 0, 0, 1};
                        if (frag_coord) {
                            memcpy(val, frag_coord, 4 * sizeof(float));
                        }
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_load_front_face: {
                        mp_reg r;
                        r.u = front_face ? ~0u : 0u;
                        float val[4] = { r.f, 0, 0, 0 };
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_load_vertex_id:
                    case nir_intrinsic_load_instance_id: {
                        float val[4] = {0, 0, 0, 0};
                        write_dest(&intrin->def, t, val);
                        break;
                    }

                    case nir_intrinsic_demote:
                    case nir_intrinsic_terminate:
                        if (discard_flag) *discard_flag = true;
                        break;

                    case nir_intrinsic_demote_if:
                    case nir_intrinsic_terminate_if: {
                        float cond[4];
                        read_src(&intrin->src[0], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, cond);
                        mp_reg cr;
                        cr.f = cond[0];
                        if (cr.u && discard_flag)
                            *discard_flag = true;
                        break;
                    }

                    default:
                        break;
                    }
                    break;
                }

                case nir_instr_type_tex: {
                    nir_tex_instr *tex = nir_instr_as_tex(instr);
                    float coord[4] = {0, 0, 0, 0};
                    float projector = 1.0f;

                    /* Find coordinate and projector sources */
                    for (unsigned s = 0; s < tex->num_srcs; s++) {
                        if (tex->src[s].src_type == nir_tex_src_coord) {
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, coord);
                        } else if (tex->src[s].src_type == nir_tex_src_projector) {
                            float proj[4];
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, proj);
                            projector = proj[0];
                        }
                    }

                    /* Apply projective divide */
                    float s_coord = coord[0];
                    float t_coord = coord[1];
                    if (projector != 0.0f && projector != 1.0f) {
                        float inv_proj = 1.0f / projector;
                        s_coord *= inv_proj;
                        t_coord *= inv_proj;
                    }

                    float result[4] = {0, 0, 0, 1};
                    sample_texture_2d(ctx, tex->sampler_index, stage,
                                      s_coord, t_coord, result);
                    write_dest(&tex->def, t, result);
                    break;
                }

                case nir_instr_type_jump:
                    /* Simple: just skip — actual control flow not needed for linear shaders */
                    break;

                default:
                    break;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* VS wrapper                                                          */
/* ------------------------------------------------------------------ */

void mp_run_vs(const struct mp_compiled_shader *shader,
               const struct mypipe_context *ctx,
               const float inputs[][4], unsigned num_inputs,
               const void *uniforms, struct mp_vs_output *out) {
    if (!shader || !shader->nir) {
        /* Fallback: passthrough position */
        out->position[0] = inputs[0][0];
        out->position[1] = inputs[0][1];
        out->position[2] = inputs[0][2];
        out->position[3] = 1.0f;
        out->num_varyings = 0;
        return;
    }

    float in_copy[MP_MAX_ATTRIBS][4];
    for (unsigned i = 0; i < num_inputs && i < MP_MAX_ATTRIBS; i++)
        memcpy(in_copy[i], inputs[i], 4 * sizeof(float));

    float outputs[MP_MAX_VARYINGS + 2][4]; /* +2: slot 0=pos, 1..N=vary, N+1=psiz */
    memset(outputs, 0, sizeof(outputs));
    unsigned num_outputs = 0;

    mp_exec_shader(shader, ctx, MESA_SHADER_VERTEX,
                    in_copy, num_inputs,
                    outputs, &num_outputs,
                    uniforms, NULL, true, NULL);

    /* Output slot 0 = gl_Position */
    memcpy(out->position, outputs[0], 4 * sizeof(float));

    /* Remaining slots = varyings (skip PSIZ slot which is at MP_MAX_VARYINGS+1) */
    out->num_varyings = (num_outputs > 1) ? num_outputs - 1 : 0;
    if (out->num_varyings > MP_MAX_VARYINGS)
        out->num_varyings = MP_MAX_VARYINGS;
    for (unsigned i = 0; i < out->num_varyings && i < MP_MAX_VARYINGS; i++)
        memcpy(out->varyings[i], outputs[1 + i], 4 * sizeof(float));

    /* gl_PointSize stored at special slot */
    out->point_size = outputs[MP_MAX_VARYINGS + 1][0];
}

/* ------------------------------------------------------------------ */
/* FS wrapper                                                          */
/* ------------------------------------------------------------------ */

void mp_run_fs(const struct mp_compiled_shader *shader,
               const struct mypipe_context *ctx,
               const void *uniforms, struct mp_quad *quad) {
    if (!shader || !shader->nir) {
        /* Fallback: red */
        for (int p = 0; p < 4; p++) {
            quad->color_out[p][0] = 1.0f;
            quad->color_out[p][1] = 0.0f;
            quad->color_out[p][2] = 0.0f;
            quad->color_out[p][3] = 1.0f;
        }
        return;
    }

    for (int p = 0; p < 4; p++) {
        /* Copy interpolated varyings into inputs */
        float fs_inputs[MP_MAX_VARYINGS][4];
        unsigned num_varyings = 0;

        for (unsigned v = 0; v < MP_MAX_VARYINGS; v++) {
            memcpy(fs_inputs[v], quad->varyings[v][p], 4 * sizeof(float));
            /* Count active varyings */
            if (quad->varyings[v][p][0] != 0 || quad->varyings[v][p][1] != 0 ||
                quad->varyings[v][p][2] != 0 || quad->varyings[v][p][3] != 0)
                num_varyings = v + 1;
        }

        float outputs[2][4];
        memset(outputs, 0, sizeof(outputs));
        unsigned num_outputs = 0;
        bool discard = false;

        mp_exec_shader(shader, ctx, MESA_SHADER_FRAGMENT,
                        fs_inputs, num_varyings > 0 ? num_varyings : MP_MAX_VARYINGS,
                        outputs, &num_outputs,
                        uniforms,
                        quad->frag_coord[p],
                        quad->front_face,
                        &discard);

        if (discard) {
            quad->mask &= ~(1 << p);
            quad->discard[p] = true;
        }

        memcpy(quad->color_out[p], outputs[0], 4 * sizeof(float));
    }
}
