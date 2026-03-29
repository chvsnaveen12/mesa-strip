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

/* Sample one mip level */
static void sample_texture_2d_level(const struct mypipe_resource *mpr,
                                    struct pipe_sampler_view *view,
                                    struct pipe_sampler_state *samp,
                                    unsigned level,
                                    float s, float t,
                                    float out[4]) {
    unsigned w = MAX2(1, view->texture->width0 >> level);
    unsigned h = MAX2(1, view->texture->height0 >> level);
    enum pipe_format format = view->format;

    uint8_t *data = (uint8_t *)mpr->data + mpr->level_offset[level];
    unsigned stride = mpr->stride[level];

    float fx = apply_wrap(s, samp->wrap_s, w);
    float fy = apply_wrap(t, samp->wrap_t, h);

    if (samp->min_img_filter == PIPE_TEX_FILTER_LINEAR ||
        samp->mag_img_filter == PIPE_TEX_FILTER_LINEAR) {
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
        int ix = (int)floorf(fx);
        int iy = (int)floorf(fy);
        fetch_texel(data, stride, format, ix, iy, w, h, out);
    }
}

static void sample_texture_2d(const struct mypipe_context *ctx,
                               unsigned sampler_idx,
                               mesa_shader_stage stage,
                               float s, float t,
                               float dsdx, float dsdy,
                               float dtdx, float dtdy,
                               float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;

    if (sampler_idx >= MP_MAX_SAMPLERS) return;

    struct pipe_sampler_view *view = ctx->sampler_views[stage][sampler_idx];
    struct pipe_sampler_state *samp = ctx->samplers[stage][sampler_idx];
    if (!view || !samp || !view->texture) return;

    struct mypipe_resource *mpr = mypipe_resource(view->texture);
    if (!mpr->data) return;

    /* For display targets, sample level 0 from the display target */
    if (mpr->dt) {
        struct sw_winsys *winsys = mypipe_screen(view->texture->screen)->winsys;
        uint8_t *dt_data = winsys->displaytarget_map(winsys, mpr->dt, PIPE_MAP_READ);
        if (!dt_data) return;
        unsigned w = view->texture->width0;
        unsigned h = view->texture->height0;
        float fx = apply_wrap(s, samp->wrap_s, w);
        float fy = apply_wrap(t, samp->wrap_t, h);
        int ix = (int)floorf(fx);
        int iy = (int)floorf(fy);
        fetch_texel(dt_data, mpr->stride[0], view->format, ix, iy, w, h, out);
        winsys->displaytarget_unmap(winsys, mpr->dt);
    } else {
        /* Compute LOD from screen-space derivatives */
        unsigned last_level = view->texture->last_level;
        unsigned level = 0;

        if (last_level > 0) {
            float w0 = (float)view->texture->width0;
            float h0 = (float)view->texture->height0;
            float dudx = dsdx * w0, dudy = dsdy * w0;
            float dvdx = dtdx * h0, dvdy = dtdy * h0;
            float rho_x = sqrtf(dudx * dudx + dvdx * dvdx);
            float rho_y = sqrtf(dudy * dudy + dvdy * dvdy);
            float rho = fmaxf(rho_x, rho_y);
            if (rho > 1.0f) {
                float lod = log2f(rho);
                level = (unsigned)CLAMP((int)(lod + 0.5f), 0, (int)last_level);
            }
        }

        sample_texture_2d_level(mpr, view, samp, level, s, t, out);
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
/* Core NIR interpreter with proper control flow                       */
/* ------------------------------------------------------------------ */

enum mp_flow { MP_FLOW_NORMAL = 0, MP_FLOW_BREAK, MP_FLOW_CONTINUE, MP_FLOW_RETURN };

struct mp_interp_state {
    float (*t)[4]; /* SSA temps [MP_MAX_SSA][4] */
    float hw[MP_MAX_HW_REGS][4];
    const struct mp_compiled_shader *shader;
    const struct mypipe_context *ctx;
    mesa_shader_stage stage;
    float (*inputs)[4];
    unsigned num_inputs;
    float (*outputs)[4];
    unsigned *num_outputs;
    const void *uniforms;
    float *frag_coord;
    bool front_face;
    bool *discard_flag;
    /* Per-quad varying derivatives for LOD computation */
    float quad_dvdx[MP_MAX_VARYINGS][4]; /* d(varying)/dx */
    float quad_dvdy[MP_MAX_VARYINGS][4]; /* d(varying)/dy */
    /* Track which FS input varying each SSA value came from (-1 = not a varying) */
    int *ssa_src_varying;
};

static enum mp_flow exec_cf_list(struct mp_interp_state *st, struct exec_list *cf_list);

static enum mp_flow exec_block(struct mp_interp_state *st, nir_block *block) {
    const struct mp_compiled_shader *shader = st->shader;
    float (*t)[4] = st->t;
    float (*hw)[4] = st->hw;
    mesa_shader_stage stage = st->stage;
    unsigned num_inputs = st->num_inputs;
    float (*inputs)[4] = st->inputs;
    float (*outputs)[4] = st->outputs;
    const void *uniforms = st->uniforms;
    float *frag_coord = st->frag_coord;
    bool front_face = st->front_face;

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
                        case nir_op_fround_even: dst[c] = rintf(a); break;
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
                        case nir_op_b2b1:    { mp_reg cond; cond.f = a; mp_reg r; r.u = cond.u ? 1u : 0u; dst[c] = r.f; break; }
                        case nir_op_b2b32:   { mp_reg cond; cond.f = a; mp_reg r; r.u = cond.u ? ~0u : 0u; dst[c] = r.f; break; }
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
                    /* Propagate source-varying tag for mov/vec ops */
                    if (st->ssa_src_varying) {
                        unsigned di = alu->def.index;
                        if (di < MP_MAX_SSA) {
                            if (alu->op == nir_op_mov) {
                                unsigned si = alu->src[0].src.ssa->index;
                                st->ssa_src_varying[di] = (si < MP_MAX_SSA) ? st->ssa_src_varying[si] : -1;
                            } else if (alu->op >= nir_op_vec2 && alu->op <= nir_op_vec4) {
                                /* Use the tag from the first source */
                                unsigned si = alu->src[0].src.ssa->index;
                                st->ssa_src_varying[di] = (si < MP_MAX_SSA) ? st->ssa_src_varying[si] : -1;
                            } else {
                                st->ssa_src_varying[di] = -1;
                            }
                        }
                    }
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
                        unsigned slot = nir_intrinsic_base(intrin);
                        unsigned comp = nir_intrinsic_component(intrin);
                        unsigned io_loc = nir_intrinsic_io_semantics(intrin).location;
                        float val[4] = {0, 0, 0, 0};

                        if (stage == MESA_SHADER_FRAGMENT &&
                            io_loc == VARYING_SLOT_FACE) {
                            /* gl_FrontFacing: return as NIR bool */
                            mp_reg r;
                            r.u = front_face ? ~0u : 0u;
                            val[0] = r.f;
                        } else if (stage == MESA_SHADER_FRAGMENT &&
                                   io_loc == VARYING_SLOT_POS) {
                            /* gl_FragCoord */
                            if (frag_coord) {
                                for (unsigned c = 0; c < intrin->def.num_components; c++)
                                    val[c] = frag_coord[comp + c];
                            }
                        } else if (stage == MESA_SHADER_FRAGMENT) {
                            /* FS user varyings: nir_lower_io assigns base numbers
                             * to ALL declared inputs including system values (POS,
                             * FACE). Subtract the system-value base slots to get
                             * the correct index into our varyings array. */
                            unsigned offset = shader->fs_input_base_offset;
                            unsigned vary_idx = (slot >= offset) ? slot - offset : 0;
                            if (vary_idx < (unsigned)num_inputs) {
                                for (unsigned c = 0; c < intrin->def.num_components; c++)
                                    val[c] = inputs[vary_idx][comp + c];
                            }
                        } else {
                            /* VS: base maps directly to vertex attribute index */
                            if (slot < (unsigned)num_inputs) {
                                for (unsigned c = 0; c < intrin->def.num_components; c++)
                                    val[c] = inputs[slot][comp + c];
                            }
                        }
                        write_dest(&intrin->def, t, val);
                        /* Tag this SSA with its source varying index for LOD derivatives */
                        if (st->ssa_src_varying && stage == MESA_SHADER_FRAGMENT &&
                            io_loc != VARYING_SLOT_FACE && io_loc != VARYING_SLOT_POS) {
                            unsigned offset = shader->fs_input_base_offset;
                            unsigned vary_idx = (slot >= offset) ? slot - offset : 0;
                            unsigned def_idx = intrin->def.index;
                            if (def_idx < MP_MAX_SSA)
                                st->ssa_src_varying[def_idx] = (int)vary_idx;
                        }
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
                        if (slot >= *st->num_outputs)
                            *st->num_outputs = slot + 1;
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
                        if (st->discard_flag) *st->discard_flag = true;
                        break;

                    case nir_intrinsic_demote_if:
                    case nir_intrinsic_terminate_if: {
                        float cond[4];
                        read_src(&intrin->src[0], (const float (*)[4])t,
                                 (const float (*)[4])hw, shader, cond);
                        mp_reg cr;
                        cr.f = cond[0];
                        if (cr.u && st->discard_flag)
                            *st->discard_flag = true;
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
                    float ddx[4] = {0, 0, 0, 0};
                    float ddy[4] = {0, 0, 0, 0};
                    bool has_derivs = false;

                    for (unsigned s = 0; s < tex->num_srcs; s++) {
                        if (tex->src[s].src_type == nir_tex_src_coord) {
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, coord);
                        } else if (tex->src[s].src_type == nir_tex_src_projector) {
                            float proj[4];
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, proj);
                            projector = proj[0];
                        } else if (tex->src[s].src_type == nir_tex_src_ddx) {
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, ddx);
                            has_derivs = true;
                        } else if (tex->src[s].src_type == nir_tex_src_ddy) {
                            read_src(&tex->src[s].src, (const float (*)[4])t,
                                     (const float (*)[4])hw, shader, ddy);
                        }
                    }

                    float s_coord = coord[0];
                    float t_coord = coord[1];
                    if (projector != 0.0f && projector != 1.0f) {
                        float inv_proj = 1.0f / projector;
                        s_coord *= inv_proj;
                        t_coord *= inv_proj;
                    }

                    /* Compute derivatives for LOD */
                    float use_dsdx = 0, use_dsdy = 0, use_dtdx = 0, use_dtdy = 0;
                    if (has_derivs) {
                        use_dsdx = ddx[0]; use_dsdy = ddx[1];
                        use_dtdx = ddy[0]; use_dtdy = ddy[1];
                    } else if (st->ssa_src_varying) {
                        /* Look up which varying the coord came from */
                        int src_vary = -1;
                        for (unsigned si = 0; si < tex->num_srcs; si++) {
                            if (tex->src[si].src_type == nir_tex_src_coord) {
                                unsigned ci = tex->src[si].src.ssa->index;
                                if (ci < MP_MAX_SSA)
                                    src_vary = st->ssa_src_varying[ci];
                                break;
                            }
                        }
                        if (src_vary >= 0 && src_vary < MP_MAX_VARYINGS) {
                            use_dsdx = st->quad_dvdx[src_vary][0];
                            use_dsdy = st->quad_dvdy[src_vary][0];
                            use_dtdx = st->quad_dvdx[src_vary][1];
                            use_dtdy = st->quad_dvdy[src_vary][1];
                        }
                    }

                    float result[4] = {0, 0, 0, 1};
                    sample_texture_2d(st->ctx, tex->sampler_index, stage,
                                      s_coord, t_coord,
                                      use_dsdx, use_dsdy,
                                      use_dtdx, use_dtdy,
                                      result);
                    write_dest(&tex->def, t, result);
                    break;
                }

                case nir_instr_type_jump: {
                    nir_jump_instr *jump = nir_instr_as_jump(instr);
                    switch (jump->type) {
                    case nir_jump_break:    return MP_FLOW_BREAK;
                    case nir_jump_continue: return MP_FLOW_CONTINUE;
                    case nir_jump_return:   return MP_FLOW_RETURN;
                    default: break;
                    }
                    break;
                }

                default:
                    break;
                }
            }
    return MP_FLOW_NORMAL;
}

static enum mp_flow exec_cf_list(struct mp_interp_state *st, struct exec_list *cf_list) {
    foreach_list_typed(nir_cf_node, node, node, cf_list) {
        enum mp_flow flow;
        switch (node->type) {
        case nir_cf_node_block:
            flow = exec_block(st, nir_cf_node_as_block(node));
            if (flow != MP_FLOW_NORMAL) return flow;
            break;
        case nir_cf_node_if: {
            nir_if *nif = nir_cf_node_as_if(node);
            float cond_val[4];
            read_src(&nif->condition, (const float (*)[4])st->t,
                     (const float (*)[4])st->hw, st->shader, cond_val);
            mp_reg cr; cr.f = cond_val[0];
            flow = cr.u ? exec_cf_list(st, &nif->then_list)
                        : exec_cf_list(st, &nif->else_list);
            if (flow != MP_FLOW_NORMAL) return flow;
            break;
        }
        case nir_cf_node_loop: {
            nir_loop *loop = nir_cf_node_as_loop(node);
            for (int iter = 0; iter < 4096; iter++) {
                flow = exec_cf_list(st, &loop->body);
                if (flow == MP_FLOW_BREAK) break;
                if (flow == MP_FLOW_RETURN) return MP_FLOW_RETURN;
            }
            break;
        }
        default: break;
        }
    }
    return MP_FLOW_NORMAL;
}

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

    static float ssa_temps[MP_MAX_SSA][4];
    memset(ssa_temps, 0, sizeof(ssa_temps));

    struct mp_interp_state st;
    memset(&st, 0, sizeof(st));
    st.t = ssa_temps;
    st.shader = shader;
    st.ctx = ctx;
    st.stage = stage;
    st.inputs = inputs;
    st.num_inputs = num_inputs;
    st.outputs = outputs;
    st.num_outputs = num_outputs;
    st.uniforms = uniforms;
    st.frag_coord = frag_coord;
    st.front_face = front_face;
    st.discard_flag = discard_flag;

    *num_outputs = 0;

    nir_foreach_function_impl(impl, nir) {
        exec_cf_list(&st, &impl->body);
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
        for (int p = 0; p < 4; p++) {
            quad->color_out[p][0] = 1.0f;
            quad->color_out[p][1] = 0.0f;
            quad->color_out[p][2] = 0.0f;
            quad->color_out[p][3] = 1.0f;
        }
        return;
    }

    unsigned num_varyings = quad->num_varyings;
    if (num_varyings == 0)
        num_varyings = MP_MAX_VARYINGS;

    /* Pre-compute per-quad varying derivatives from the 2x2 quad.
     * pixel 0=(x,y), 1=(x+1,y), 2=(x,y+1), 3=(x+1,y+1)
     * dFdx = pixel1 - pixel0,  dFdy = pixel2 - pixel0 */
    float dvdx[MP_MAX_VARYINGS][4];
    float dvdy[MP_MAX_VARYINGS][4];
    for (unsigned v = 0; v < num_varyings; v++) {
        for (int c = 0; c < 4; c++) {
            dvdx[v][c] = quad->varyings[v][1][c] - quad->varyings[v][0][c];
            dvdy[v][c] = quad->varyings[v][2][c] - quad->varyings[v][0][c];
        }
    }

    /* SSA source-varying tracker for derivative propagation */
    static int ssa_src_vary[MP_MAX_SSA];

    for (int p = 0; p < 4; p++) {
        float fs_inputs[MP_MAX_VARYINGS][4];
        for (unsigned v = 0; v < num_varyings; v++)
            memcpy(fs_inputs[v], quad->varyings[v][p], 4 * sizeof(float));

        float outputs[2][4];
        memset(outputs, 0, sizeof(outputs));
        unsigned num_outputs = 0;
        bool discard = false;

        /* Reset varying tags */
        memset(ssa_src_vary, -1, sizeof(ssa_src_vary));

        /* Set up the interp state with quad derivatives.
         * mp_exec_shader builds st internally, so we need to set the
         * derivatives on the state AFTER it's built. We do this by
         * calling the internal exec_cf_list directly. */
        static float ssa_temps[MP_MAX_SSA][4];
        memset(ssa_temps, 0, sizeof(ssa_temps));

        struct mp_interp_state st;
        memset(&st, 0, sizeof(st));
        st.t = ssa_temps;
        st.shader = shader;
        st.ctx = ctx;
        st.stage = MESA_SHADER_FRAGMENT;
        st.inputs = fs_inputs;
        st.num_inputs = num_varyings;
        st.outputs = outputs;
        st.num_outputs = &num_outputs;
        st.uniforms = uniforms;
        st.frag_coord = quad->frag_coord[p];
        st.front_face = quad->front_face;
        st.discard_flag = &discard;
        memcpy(st.quad_dvdx, dvdx, sizeof(dvdx));
        memcpy(st.quad_dvdy, dvdy, sizeof(dvdy));
        st.ssa_src_varying = ssa_src_vary;

        num_outputs = 0;
        nir_foreach_function_impl(impl, shader->nir) {
            exec_cf_list(&st, &impl->body);
        }

        if (discard) {
            quad->mask &= ~(1 << p);
            quad->discard[p] = true;
        }

        memcpy(quad->color_out[p], outputs[0], 4 * sizeof(float));
    }
}
