#include <stdio.h>
#include <math.h>
#include <string.h>

#include "pipe/p_defines.h"
#include "util/u_math.h"

#include "mp_raster.h"
#include "mp_shader_exec.h"
#include "mp_context.h"
#include "mp_texture.h"

/* ------------------------------------------------------------ */
/* Edge function                                                 */
/* ------------------------------------------------------------ */

static inline float edge_func(float v0x, float v0y,
                               float v1x, float v1y,
                               float px,  float py) {
    return (px - v0x) * (v1y - v0y) - (py - v0y) * (v1x - v0x);
}

/* ------------------------------------------------------------ */
/* Depth buffer access helpers                                   */
/* ------------------------------------------------------------ */

static float read_depth(struct mp_framebuffer *fb, unsigned x, unsigned y,
                        enum pipe_format zfmt) {
    if (!fb->depth_map) return 1.0f;
    uint8_t *row = fb->depth_map + y * fb->depth_stride;

    switch (zfmt) {
    case PIPE_FORMAT_Z24_UNORM_S8_UINT:
    case PIPE_FORMAT_Z24X8_UNORM:
        return (float)(((uint32_t *)row)[x] & 0x00FFFFFF) / (float)0xFFFFFF;
    case PIPE_FORMAT_S8_UINT_Z24_UNORM:
    case PIPE_FORMAT_X8Z24_UNORM:
        return (float)(((uint32_t *)row)[x] >> 8) / (float)0xFFFFFF;
    case PIPE_FORMAT_Z32_FLOAT:
        return ((float *)row)[x];
    case PIPE_FORMAT_Z16_UNORM:
        return (float)((uint16_t *)row)[x] / 65535.0f;
    default:
        return 1.0f;
    }
}

static void write_depth(struct mp_framebuffer *fb, unsigned x, unsigned y,
                        float z, enum pipe_format zfmt) {
    if (!fb->depth_map) return;
    uint8_t *row = fb->depth_map + y * fb->depth_stride;
    uint32_t z24 = (uint32_t)(CLAMP(z, 0.0f, 1.0f) * (float)0xFFFFFF);

    switch (zfmt) {
    case PIPE_FORMAT_Z24_UNORM_S8_UINT:
    case PIPE_FORMAT_Z24X8_UNORM: {
        uint32_t *ptr = &((uint32_t *)row)[x];
        *ptr = (*ptr & 0xFF000000) | (z24 & 0x00FFFFFF);
        break;
    }
    case PIPE_FORMAT_S8_UINT_Z24_UNORM:
    case PIPE_FORMAT_X8Z24_UNORM: {
        uint32_t *ptr = &((uint32_t *)row)[x];
        *ptr = (*ptr & 0xFF) | ((z24 & 0x00FFFFFF) << 8);
        break;
    }
    case PIPE_FORMAT_Z32_FLOAT:
        ((float *)row)[x] = z;
        break;
    case PIPE_FORMAT_Z16_UNORM:
        ((uint16_t *)row)[x] = (uint16_t)(CLAMP(z, 0.0f, 1.0f) * 65535.0f);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ */
/* Stencil buffer access helpers                                 */
/* ------------------------------------------------------------ */

static uint8_t read_stencil(struct mp_framebuffer *fb, unsigned x, unsigned y) {
    if (!fb->stencil_map) return 0;
    uint8_t *row = fb->stencil_map + y * fb->depth_stride;
    uint32_t val = ((uint32_t *)row)[x];
    return (uint8_t)(val >> 24);
}

static void write_stencil(struct mp_framebuffer *fb, unsigned x, unsigned y, uint8_t val) {
    if (!fb->stencil_map) return;
    uint8_t *row = fb->stencil_map + y * fb->depth_stride;
    uint32_t *ptr = &((uint32_t *)row)[x];
    *ptr = (*ptr & 0x00FFFFFF) | ((uint32_t)val << 24);
}

static uint8_t apply_stencil_op(unsigned op, uint8_t cur, uint8_t ref) {
    switch (op) {
    case PIPE_STENCIL_OP_KEEP:      return cur;
    case PIPE_STENCIL_OP_ZERO:      return 0;
    case PIPE_STENCIL_OP_REPLACE:   return ref;
    case PIPE_STENCIL_OP_INCR:      return (cur < 255) ? cur + 1 : 255;
    case PIPE_STENCIL_OP_DECR:      return (cur > 0) ? cur - 1 : 0;
    case PIPE_STENCIL_OP_INVERT:    return ~cur;
    case PIPE_STENCIL_OP_INCR_WRAP: return cur + 1;
    case PIPE_STENCIL_OP_DECR_WRAP: return cur - 1;
    default: return cur;
    }
}

/* ------------------------------------------------------------ */
/* Per-fragment: Scissor test                                    */
/* ------------------------------------------------------------ */

static void scissor_test(struct mp_quad *quad, struct mypipe_context *mypipe,
                         struct mp_framebuffer *fb) {
    if (!mypipe->rasterizer || !mypipe->rasterizer->scissor)
        return;

    static const int dx[4] = {0, 1, 0, 1};
    static const int dy[4] = {0, 0, 1, 1};
    struct pipe_scissor_state *sc = &mypipe->scissor;

    for (int p = 0; p < 4; p++) {
        if (!(quad->mask & (1 << p))) continue;
        unsigned px = quad->x + dx[p];
        unsigned py = quad->y + dy[p];
        if (px < sc->minx || px >= sc->maxx ||
            py < sc->miny || py >= sc->maxy)
            quad->mask &= ~(1 << p);
    }
}

/* ------------------------------------------------------------ */
/* Per-fragment: Depth test                                      */
/* ------------------------------------------------------------ */

static bool compare_depth(unsigned func, float frag_z, float buf_z) {
    switch (func) {
    case PIPE_FUNC_NEVER:    return false;
    case PIPE_FUNC_LESS:     return frag_z < buf_z;
    case PIPE_FUNC_EQUAL:    return frag_z == buf_z;
    case PIPE_FUNC_LEQUAL:   return frag_z <= buf_z;
    case PIPE_FUNC_GREATER:  return frag_z > buf_z;
    case PIPE_FUNC_NOTEQUAL: return frag_z != buf_z;
    case PIPE_FUNC_GEQUAL:   return frag_z >= buf_z;
    case PIPE_FUNC_ALWAYS:   return true;
    default: return true;
    }
}

static void depth_test(struct mp_quad *quad, struct mypipe_context *mypipe,
                       struct mp_framebuffer *fb, enum pipe_format zfmt) {
    if (!mypipe->depth_stencil || !mypipe->depth_stencil->depth_enabled)
        return;
    if (!fb->depth_map)
        return;

    static const int dx[4] = {0, 1, 0, 1};
    static const int dy[4] = {0, 0, 1, 1};

    for (int p = 0; p < 4; p++) {
        if (!(quad->mask & (1 << p))) continue;

        unsigned px = quad->x + dx[p];
        unsigned py = quad->y + dy[p];
        float frag_z = CLAMP(quad->z[p], 0.0f, 1.0f);
        float buf_z = read_depth(fb, px, py, zfmt);

        if (!compare_depth(mypipe->depth_stencil->depth_func, frag_z, buf_z)) {
            quad->mask &= ~(1 << p);
            continue;
        }

        if (mypipe->depth_stencil->depth_writemask)
            write_depth(fb, px, py, frag_z, zfmt);
    }
}

/* ------------------------------------------------------------ */
/* Per-fragment: Stencil test                                    */
/* ------------------------------------------------------------ */

static void stencil_test(struct mp_quad *quad, struct mypipe_context *mypipe,
                         struct mp_framebuffer *fb, enum pipe_format zfmt,
                         bool depth_passed[4]) {
    if (!mypipe->depth_stencil || !mypipe->depth_stencil->stencil[0].enabled)
        return;
    if (!fb->stencil_map)
        return;

    static const int dx[4] = {0, 1, 0, 1};
    static const int dy[4] = {0, 0, 1, 1};

    const struct pipe_stencil_state *ss = &mypipe->depth_stencil->stencil[0];
    uint8_t ref = mypipe->stencil_ref.ref_value[0] & ss->valuemask;

    for (int p = 0; p < 4; p++) {
        /* Process all originally-covered pixels for stencil ops */
        unsigned px = quad->x + dx[p];
        unsigned py = quad->y + dy[p];
        if (px >= fb->width || py >= fb->height) continue;

        uint8_t cur = read_stencil(fb, px, py) & ss->valuemask;
        bool stencil_pass = compare_depth(ss->func, ref, cur);

        if (!stencil_pass) {
            quad->mask &= ~(1 << p);
            uint8_t new_val = apply_stencil_op(ss->fail_op, cur, mypipe->stencil_ref.ref_value[0]);
            if (ss->writemask)
                write_stencil(fb, px, py, (read_stencil(fb, px, py) & ~ss->writemask) |
                              (new_val & ss->writemask));
        } else if (!depth_passed[p]) {
            quad->mask &= ~(1 << p);
            uint8_t new_val = apply_stencil_op(ss->zfail_op, cur, mypipe->stencil_ref.ref_value[0]);
            if (ss->writemask)
                write_stencil(fb, px, py, (read_stencil(fb, px, py) & ~ss->writemask) |
                              (new_val & ss->writemask));
        } else {
            uint8_t new_val = apply_stencil_op(ss->zpass_op, cur, mypipe->stencil_ref.ref_value[0]);
            if (ss->writemask)
                write_stencil(fb, px, py, (read_stencil(fb, px, py) & ~ss->writemask) |
                              (new_val & ss->writemask));
        }
    }
}

/* ------------------------------------------------------------ */
/* Per-fragment: Blend                                           */
/* ------------------------------------------------------------ */

static float blend_factor(unsigned factor, float src_c, float src_a,
                          float dst_c, float dst_a,
                          const struct pipe_blend_color *bc, int comp) {
    switch (factor) {
    case PIPE_BLENDFACTOR_ZERO:           return 0.0f;
    case PIPE_BLENDFACTOR_ONE:            return 1.0f;
    case PIPE_BLENDFACTOR_SRC_COLOR:      return src_c;
    case PIPE_BLENDFACTOR_SRC_ALPHA:      return src_a;
    case PIPE_BLENDFACTOR_DST_COLOR:      return dst_c;
    case PIPE_BLENDFACTOR_DST_ALPHA:      return dst_a;
    case PIPE_BLENDFACTOR_INV_SRC_COLOR:  return 1.0f - src_c;
    case PIPE_BLENDFACTOR_INV_SRC_ALPHA:  return 1.0f - src_a;
    case PIPE_BLENDFACTOR_INV_DST_COLOR:  return 1.0f - dst_c;
    case PIPE_BLENDFACTOR_INV_DST_ALPHA:  return 1.0f - dst_a;
    case PIPE_BLENDFACTOR_CONST_COLOR:    return bc ? bc->color[comp] : 0.0f;
    case PIPE_BLENDFACTOR_INV_CONST_COLOR: return bc ? (1.0f - bc->color[comp]) : 1.0f;
    case PIPE_BLENDFACTOR_CONST_ALPHA:    return bc ? bc->color[3] : 0.0f;
    case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return bc ? (1.0f - bc->color[3]) : 1.0f;
    case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: {
        float f = fminf(src_a, 1.0f - dst_a);
        return (comp < 3) ? f : 1.0f;
    }
    default: return 1.0f;
    }
}

static float blend_equation(unsigned eq, float src, float dst) {
    switch (eq) {
    case PIPE_BLEND_ADD:              return src + dst;
    case PIPE_BLEND_SUBTRACT:         return src - dst;
    case PIPE_BLEND_REVERSE_SUBTRACT: return dst - src;
    case PIPE_BLEND_MIN:              return fminf(src, dst);
    case PIPE_BLEND_MAX:              return fmaxf(src, dst);
    default: return src + dst;
    }
}

/* ------------------------------------------------------------ */
/* Pixel read/write                                              */
/* ------------------------------------------------------------ */

static void read_pixel(struct mp_framebuffer *fb,
                       unsigned x, unsigned y, float color[4]) {
    if (x >= fb->width || y >= fb->height) {
        color[0] = color[1] = color[2] = color[3] = 0;
        return;
    }

    uint8_t *pixel = fb->color_map + y * fb->color_stride + x * 4;

    switch (fb->color_format) {
    case PIPE_FORMAT_B8G8R8A8_UNORM:
        color[0] = pixel[2] / 255.0f;
        color[1] = pixel[1] / 255.0f;
        color[2] = pixel[0] / 255.0f;
        color[3] = pixel[3] / 255.0f;
        break;
    case PIPE_FORMAT_R8G8B8A8_UNORM:
        color[0] = pixel[0] / 255.0f;
        color[1] = pixel[1] / 255.0f;
        color[2] = pixel[2] / 255.0f;
        color[3] = pixel[3] / 255.0f;
        break;
    case PIPE_FORMAT_B8G8R8X8_UNORM:
        color[0] = pixel[2] / 255.0f;
        color[1] = pixel[1] / 255.0f;
        color[2] = pixel[0] / 255.0f;
        color[3] = 1.0f;
        break;
    case PIPE_FORMAT_R8G8B8X8_UNORM:
        color[0] = pixel[0] / 255.0f;
        color[1] = pixel[1] / 255.0f;
        color[2] = pixel[2] / 255.0f;
        color[3] = 1.0f;
        break;
    default:
        color[0] = color[1] = color[2] = 0;
        color[3] = 1;
        break;
    }
}

static void write_pixel(struct mp_framebuffer *fb,
                        struct mypipe_context *mypipe,
                        unsigned x, unsigned y,
                        const float src_color[4]) {
    if (x >= fb->width || y >= fb->height) return;

    float final[4];
    memcpy(final, src_color, sizeof(final));

    /* Blend */
    if (mypipe->blend && mypipe->blend->rt[0].blend_enable) {
        const struct pipe_rt_blend_state *rt = &mypipe->blend->rt[0];
        float dst[4];
        read_pixel(fb, x, y, dst);

        for (int c = 0; c < 4; c++) {
            unsigned src_factor_e = (c < 3) ? rt->rgb_src_factor : rt->alpha_src_factor;
            unsigned dst_factor_e = (c < 3) ? rt->rgb_dst_factor : rt->alpha_dst_factor;
            unsigned eq = (c < 3) ? rt->rgb_func : rt->alpha_func;

            float sf = blend_factor(src_factor_e, final[c], final[3],
                                    dst[c], dst[3], &mypipe->blend_color, c);
            float df = blend_factor(dst_factor_e, final[c], final[3],
                                    dst[c], dst[3], &mypipe->blend_color, c);

            final[c] = blend_equation(eq, final[c] * sf, dst[c] * df);
        }
    }

    /* Color write mask */
    unsigned colormask = 0xF; /* default: write all */
    if (mypipe->blend)
        colormask = mypipe->blend->rt[0].colormask;

    uint8_t r = (uint8_t)(CLAMP(final[0], 0.0f, 1.0f) * 255.0f);
    uint8_t g = (uint8_t)(CLAMP(final[1], 0.0f, 1.0f) * 255.0f);
    uint8_t b = (uint8_t)(CLAMP(final[2], 0.0f, 1.0f) * 255.0f);
    uint8_t a = (uint8_t)(CLAMP(final[3], 0.0f, 1.0f) * 255.0f);

    /* Logic op — operates on RGBA channel values */
    if (mypipe->blend && mypipe->blend->logicop_enable) {
        float dst_color[4];
        read_pixel(fb, x, y, dst_color);
        uint8_t src[4] = {r, g, b, a};
        uint8_t dst[4] = {(uint8_t)(dst_color[0]*255), (uint8_t)(dst_color[1]*255),
                          (uint8_t)(dst_color[2]*255), (uint8_t)(dst_color[3]*255)};
        unsigned op = mypipe->blend->logicop_func;
        for (int c = 0; c < 4; c++) {
            switch (op) {
            case 0:  src[c] = 0; break;                          /* CLEAR */
            case 1:  src[c] = src[c] & dst[c]; break;            /* AND */
            case 2:  src[c] = src[c] & ~dst[c]; break;           /* AND_REVERSE */
            case 3:  break;                                       /* COPY (nop) */
            case 4:  src[c] = ~src[c] & dst[c]; break;           /* AND_INVERTED */
            case 5:  src[c] = dst[c]; break;                     /* NOOP */
            case 6:  src[c] = src[c] ^ dst[c]; break;            /* XOR */
            case 7:  src[c] = src[c] | dst[c]; break;            /* OR */
            case 8:  src[c] = ~(src[c] | dst[c]); break;         /* NOR */
            case 9:  src[c] = ~(src[c] ^ dst[c]); break;         /* EQUIV */
            case 10: src[c] = ~dst[c]; break;                    /* INVERT */
            case 11: src[c] = src[c] | ~dst[c]; break;           /* OR_REVERSE */
            case 12: src[c] = ~src[c]; break;                    /* COPY_INVERTED */
            case 13: src[c] = ~src[c] | dst[c]; break;           /* OR_INVERTED */
            case 14: src[c] = ~(src[c] & dst[c]); break;         /* NAND */
            case 15: src[c] = 0xFF; break;                       /* SET */
            }
        }
        r = src[0]; g = src[1]; b = src[2]; a = src[3];
    }

    uint8_t *pixel = fb->color_map + y * fb->color_stride + x * 4;

    switch (fb->color_format) {
    case PIPE_FORMAT_B8G8R8A8_UNORM:
        if (colormask & PIPE_MASK_R) pixel[2] = r;
        if (colormask & PIPE_MASK_G) pixel[1] = g;
        if (colormask & PIPE_MASK_B) pixel[0] = b;
        if (colormask & PIPE_MASK_A) pixel[3] = a;
        break;
    case PIPE_FORMAT_R8G8B8A8_UNORM:
        if (colormask & PIPE_MASK_R) pixel[0] = r;
        if (colormask & PIPE_MASK_G) pixel[1] = g;
        if (colormask & PIPE_MASK_B) pixel[2] = b;
        if (colormask & PIPE_MASK_A) pixel[3] = a;
        break;
    case PIPE_FORMAT_B8G8R8X8_UNORM:
        if (colormask & PIPE_MASK_R) pixel[2] = r;
        if (colormask & PIPE_MASK_G) pixel[1] = g;
        if (colormask & PIPE_MASK_B) pixel[0] = b;
        pixel[3] = 0xFF;
        break;
    case PIPE_FORMAT_R8G8B8X8_UNORM:
        if (colormask & PIPE_MASK_R) pixel[0] = r;
        if (colormask & PIPE_MASK_G) pixel[1] = g;
        if (colormask & PIPE_MASK_B) pixel[2] = b;
        pixel[3] = 0xFF;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ */
/* Resolve FS uniforms                                           */
/* ------------------------------------------------------------ */

static const void *resolve_fs_uniforms(struct mypipe_context *mypipe) {
    struct pipe_constant_buffer *cbuf = &mypipe->fs_cbuf;
    if (cbuf->user_buffer)
        return cbuf->user_buffer;
    if (cbuf->buffer) {
        struct mypipe_resource *mpr = mypipe_resource(cbuf->buffer);
        if (mpr->data)
            return (const uint8_t *)mpr->data + cbuf->buffer_offset;
    }
    return NULL;
}

/* ------------------------------------------------------------ */
/* Triangle rasterizer                                           */
/* ------------------------------------------------------------ */

void mp_rasterize_triangle(struct mypipe_context *mypipe,
                           const struct mp_vertex *v0,
                           const struct mp_vertex *v1,
                           const struct mp_vertex *v2,
                           struct mp_framebuffer *fb) {
    /* Signed area of triangle (2x).  Positive = CCW, negative = CW. */
    float area = edge_func(v0->pos[0], v0->pos[1],
                           v1->pos[0], v1->pos[1],
                           v2->pos[0], v2->pos[1]);

    if (fabsf(area) < 1e-6f) return;       /* degenerate */

    float inv_area = 1.0f / area;

    /* Determine front-facing using draw module's convention:
     *   det = (v0-v2) x (v1-v2)
     *   ccw = (det < 0)
     *   face = (ccw == front_ccw) ? FRONT : BACK
     */
    float ex = v0->pos[0] - v2->pos[0];
    float ey = v0->pos[1] - v2->pos[1];
    float fx = v1->pos[0] - v2->pos[0];
    float fy = v1->pos[1] - v2->pos[1];
    float det = ex * fy - ey * fx;

    bool front_ccw = mypipe->rasterizer ? mypipe->rasterizer->front_ccw : false;
    unsigned ccw = (det < 0);
    bool front_face = (ccw == (unsigned)front_ccw);

    /* Polygon offset (depth bias) */
    float depth_offset = 0.0f;
    if (mypipe->rasterizer && mypipe->rasterizer->offset_tri) {
        /* Compute max depth slope: dz/dx and dz/dy from triangle vertices */
        float dzdx = (v1->pos[2] - v0->pos[2]) * (v2->pos[1] - v0->pos[1]) -
                     (v2->pos[2] - v0->pos[2]) * (v1->pos[1] - v0->pos[1]);
        float dzdy = (v2->pos[2] - v0->pos[2]) * (v1->pos[0] - v0->pos[0]) -
                     (v1->pos[2] - v0->pos[2]) * (v2->pos[0] - v0->pos[0]);
        dzdx *= inv_area;
        dzdy *= inv_area;
        float max_slope = fmaxf(fabsf(dzdx), fabsf(dzdy));
        float min_depth_unit = 1.0f / (float)(1 << 16); /* ~Z16 precision */
        depth_offset = mypipe->rasterizer->offset_scale * max_slope +
                       mypipe->rasterizer->offset_units * min_depth_unit;
        if (mypipe->rasterizer->offset_clamp != 0.0f)
            depth_offset = (mypipe->rasterizer->offset_clamp > 0)
                ? fminf(depth_offset, mypipe->rasterizer->offset_clamp)
                : fmaxf(depth_offset, mypipe->rasterizer->offset_clamp);
    }

    /* Get depth format */
    enum pipe_format zfmt = PIPE_FORMAT_NONE;
    if (mypipe->framebuffer.zsbuf.texture)
        zfmt = mypipe->framebuffer.zsbuf.texture->format;

    /* Resolve FS uniforms */
    const void *fs_uniforms = resolve_fs_uniforms(mypipe);

    /* Bounding box in pixel coords */
    int min_x = (int)floorf(fminf(fminf(v0->pos[0], v1->pos[0]), v2->pos[0]));
    int min_y = (int)floorf(fminf(fminf(v0->pos[1], v1->pos[1]), v2->pos[1]));
    int max_x = (int)ceilf (fmaxf(fmaxf(v0->pos[0], v1->pos[0]), v2->pos[0]));
    int max_y = (int)ceilf (fmaxf(fmaxf(v0->pos[1], v1->pos[1]), v2->pos[1]));

    /* Clamp to framebuffer */
    min_x = MAX2(min_x, 0);
    min_y = MAX2(min_y, 0);
    max_x = MIN2(max_x, (int)fb->width  - 1);
    max_y = MIN2(max_y, (int)fb->height - 1);

    /* Align to quad boundaries */
    min_x &= ~1;
    min_y &= ~1;

    /* Pixel offsets within a quad */
    static const int dx[4] = {0, 1, 0, 1};
    static const int dy[4] = {0, 0, 1, 1};

    /* Walk quads */
    for (int qy = min_y; qy <= max_y; qy += 2) {
        for (int qx = min_x; qx <= max_x; qx += 2) {

            struct mp_quad quad;
            memset(&quad, 0, sizeof(quad));
            quad.x = qx;
            quad.y = qy;
            quad.mask = 0;
            quad.front_face = front_face;

            for (int p = 0; p < 4; p++) {
                float px = qx + dx[p] + 0.5f;
                float py = qy + dy[p] + 0.5f;

                float w0 = edge_func(v1->pos[0], v1->pos[1],
                                     v2->pos[0], v2->pos[1], px, py);
                float w1 = edge_func(v2->pos[0], v2->pos[1],
                                     v0->pos[0], v0->pos[1], px, py);
                float w2 = edge_func(v0->pos[0], v0->pos[1],
                                     v1->pos[0], v1->pos[1], px, py);

                bool inside;
                if (area > 0)
                    inside = (w0 >= 0 && w1 >= 0 && w2 >= 0);
                else
                    inside = (w0 <= 0 && w1 <= 0 && w2 <= 0);

                if (inside)
                    quad.mask |= (1 << p);

                float b0 = w0 * inv_area;
                float b1 = w1 * inv_area;
                float b2 = w2 * inv_area;

                quad.bary[p][0] = b0;
                quad.bary[p][1] = b1;
                quad.bary[p][2] = b2;

                /* Interpolate depth (screen-space linear) + polygon offset */
                quad.z[p] = b0 * v0->pos[2] + b1 * v1->pos[2] + b2 * v2->pos[2] + depth_offset;

                /* Perspective-correct varying interpolation */
                float w0_inv = v0->pos[3]; /* 1/w from vertex */
                float w1_inv = v1->pos[3];
                float w2_inv = v2->pos[3];
                float denom = b0 * w0_inv + b1 * w1_inv + b2 * w2_inv;
                float scale = (fabsf(denom) > 1e-12f) ? 1.0f / denom : 0.0f;

                unsigned nv = v0->num_varyings;
                for (unsigned a = 0; a < nv; a++)
                    for (unsigned c = 0; c < 4; c++)
                        quad.varyings[a][p][c] =
                            (b0 * v0->varyings[a][c] * w0_inv +
                             b1 * v1->varyings[a][c] * w1_inv +
                             b2 * v2->varyings[a][c] * w2_inv) * scale;

                /* gl_FragCoord */
                quad.frag_coord[p][0] = px;
                quad.frag_coord[p][1] = py;
                quad.frag_coord[p][2] = quad.z[p];
                quad.frag_coord[p][3] = (fabsf(denom) > 1e-12f) ? 1.0f / denom : 1.0f;
            }

            if (quad.mask == 0) continue;

            /* --- Per-fragment pipeline --- */

            scissor_test(&quad, mypipe, fb);
            if (quad.mask == 0) continue;

            /* Fragment shader runs on full quad (for dFdx/dFdy) */
            mp_run_fs(mypipe->fs, mypipe, fs_uniforms, &quad);

            /* Alpha test — discard fragments that fail the alpha comparison */
            if (mypipe->depth_stencil && mypipe->depth_stencil->alpha_enabled) {
                float ref = mypipe->depth_stencil->alpha_ref_value;
                for (int p = 0; p < 4; p++) {
                    if (!(quad.mask & (1 << p))) continue;
                    float a = quad.color_out[p][3];
                    bool pass = compare_depth(mypipe->depth_stencil->alpha_func, a, ref);
                    if (!pass)
                        quad.mask &= ~(1 << p);
                }
            }
            if (quad.mask == 0) continue;

            /* Track which pixels passed before depth test for stencil */
            uint8_t pre_depth_mask = quad.mask;

            depth_test(&quad, mypipe, fb, zfmt);

            /* Stencil test */
            bool depth_passed[4];
            for (int p = 0; p < 4; p++)
                depth_passed[p] = !!(quad.mask & (1 << p));
            /* Restore pre-depth mask for stencil evaluation */
            uint8_t saved_mask = quad.mask;
            quad.mask = pre_depth_mask;
            stencil_test(&quad, mypipe, fb, zfmt, depth_passed);
            /* Only keep pixels that passed both depth and stencil */
            quad.mask &= saved_mask;

            if (quad.mask == 0) continue;

            /* Write surviving pixels */
            for (int p = 0; p < 4; p++) {
                if (quad.mask & (1 << p))
                    write_pixel(fb, mypipe, qx + dx[p], qy + dy[p], quad.color_out[p]);
            }
        }
    }
}

/* ------------------------------------------------------------ */
/* Line rasterizer (DDA)                                         */
/* ------------------------------------------------------------ */

void mp_rasterize_line(struct mypipe_context *mypipe,
                       const struct mp_vertex *v0,
                       const struct mp_vertex *v1,
                       struct mp_framebuffer *fb) {
    float x0 = v0->pos[0], y0 = v0->pos[1];
    float x1 = v1->pos[0], y1 = v1->pos[1];

    float dx = x1 - x0;
    float dy = y1 - y0;
    float steps = fmaxf(fabsf(dx), fabsf(dy));
    if (steps < 0.5f) steps = 1.0f;

    float x_inc = dx / steps;
    float y_inc = dy / steps;

    enum pipe_format zfmt = PIPE_FORMAT_NONE;
    if (mypipe->framebuffer.zsbuf.texture)
        zfmt = mypipe->framebuffer.zsbuf.texture->format;

    const void *fs_uniforms = resolve_fs_uniforms(mypipe);
    unsigned n_steps = (unsigned)(steps + 0.5f);

    for (unsigned i = 0; i <= n_steps; i++) {
        float t = (steps > 0) ? (float)i / steps : 0.0f;
        float px = x0 + x_inc * i;
        float py = y0 + y_inc * i;

        int ix = (int)floorf(px);
        int iy = (int)floorf(py);
        if (ix < 0 || iy < 0 || (unsigned)ix >= fb->width || (unsigned)iy >= fb->height)
            continue;

        /* Interpolate varyings */
        struct mp_quad quad;
        memset(&quad, 0, sizeof(quad));
        quad.x = ix & ~1;
        quad.y = iy & ~1;
        quad.front_face = true;

        /* Only set the one pixel */
        int pidx = (ix & 1) + ((iy & 1) << 1);
        quad.mask = 1 << pidx;

        float z = v0->pos[2] + t * (v1->pos[2] - v0->pos[2]);
        quad.z[pidx] = z;

        unsigned nv = v0->num_varyings;
        for (unsigned a = 0; a < nv; a++)
            for (unsigned c = 0; c < 4; c++)
                quad.varyings[a][pidx][c] = v0->varyings[a][c] + t * (v1->varyings[a][c] - v0->varyings[a][c]);

        quad.frag_coord[pidx][0] = px + 0.5f;
        quad.frag_coord[pidx][1] = py + 0.5f;
        quad.frag_coord[pidx][2] = z;
        quad.frag_coord[pidx][3] = 1.0f;

        mp_run_fs(mypipe->fs, mypipe, fs_uniforms, &quad);

        /* Alpha test */
        if (mypipe->depth_stencil && mypipe->depth_stencil->alpha_enabled) {
            if (!compare_depth(mypipe->depth_stencil->alpha_func,
                               quad.color_out[pidx][3],
                               mypipe->depth_stencil->alpha_ref_value))
                continue;
        }

        if (mypipe->depth_stencil && mypipe->depth_stencil->depth_enabled && fb->depth_map) {
            float buf_z = read_depth(fb, ix, iy, zfmt);
            if (!compare_depth(mypipe->depth_stencil->depth_func, CLAMP(z, 0.0f, 1.0f), buf_z))
                continue;
            if (mypipe->depth_stencil->depth_writemask)
                write_depth(fb, ix, iy, CLAMP(z, 0.0f, 1.0f), zfmt);
        }

        if (quad.mask & (1 << pidx))
            write_pixel(fb, mypipe, ix, iy, quad.color_out[pidx]);
    }
}

/* ------------------------------------------------------------ */
/* Point rasterizer                                              */
/* ------------------------------------------------------------ */

void mp_rasterize_point(struct mypipe_context *mypipe,
                        const struct mp_vertex *v,
                        struct mp_framebuffer *fb) {
    float point_size = 1.0f;
    /* Prefer per-vertex gl_PointSize from VS, fall back to fixed-function */
    if (v->point_size > 0.0f)
        point_size = v->point_size;
    else if (mypipe->rasterizer)
        point_size = mypipe->rasterizer->point_size;
    if (point_size < 1.0f) point_size = 1.0f;

    float half = point_size * 0.5f;
    float cx = v->pos[0];
    float cy = v->pos[1];

    int min_x = (int)floorf(cx - half);
    int min_y = (int)floorf(cy - half);
    int max_x = (int)ceilf(cx + half);
    int max_y = (int)ceilf(cy + half);

    min_x = MAX2(min_x, 0);
    min_y = MAX2(min_y, 0);
    max_x = MIN2(max_x, (int)fb->width - 1);
    max_y = MIN2(max_y, (int)fb->height - 1);

    enum pipe_format zfmt = PIPE_FORMAT_NONE;
    if (mypipe->framebuffer.zsbuf.texture)
        zfmt = mypipe->framebuffer.zsbuf.texture->format;

    const void *fs_uniforms = resolve_fs_uniforms(mypipe);

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            struct mp_quad quad;
            memset(&quad, 0, sizeof(quad));
            quad.x = px & ~1;
            quad.y = py & ~1;
            quad.front_face = true;

            int pidx = (px & 1) + ((py & 1) << 1);
            quad.mask = 1 << pidx;

            quad.z[pidx] = v->pos[2];

            /* Copy varyings from vertex */
            for (unsigned a = 0; a < v->num_varyings; a++)
                memcpy(quad.varyings[a][pidx], v->varyings[a], 4 * sizeof(float));

            quad.frag_coord[pidx][0] = px + 0.5f;
            quad.frag_coord[pidx][1] = py + 0.5f;
            quad.frag_coord[pidx][2] = v->pos[2];
            quad.frag_coord[pidx][3] = 1.0f;

            mp_run_fs(mypipe->fs, mypipe, fs_uniforms, &quad);

            if (mypipe->depth_stencil && mypipe->depth_stencil->depth_enabled && fb->depth_map) {
                float fz = CLAMP(v->pos[2], 0.0f, 1.0f);
                float buf_z = read_depth(fb, px, py, zfmt);
                if (!compare_depth(mypipe->depth_stencil->depth_func, fz, buf_z))
                    continue;
                if (mypipe->depth_stencil->depth_writemask)
                    write_depth(fb, px, py, fz, zfmt);
            }

            if (quad.mask & (1 << pidx))
                write_pixel(fb, mypipe, px, py, quad.color_out[pidx]);
        }
    }
}
