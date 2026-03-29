#include <stdio.h>
#include <string.h>
#include <math.h>

#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "frontend/sw_winsys.h"

#include "mp_draw.h"
#include "mp_raster.h"
#include "mp_shader_exec.h"
#include "mp_context.h"
#include "mp_screen.h"
#include "mp_texture.h"

/* ------------------------------------------------------------------ */
/* Vertex attribute fetch                                              */
/* ------------------------------------------------------------------ */

static void fetch_attrib(const uint8_t *src, enum pipe_format fmt, float out[4]){
    out[0] = out[1] = out[2] = 0;
    out[3] = 1;

    switch(fmt){
        case PIPE_FORMAT_R32_FLOAT:
            memcpy(out, src, 4);
            break;
        case PIPE_FORMAT_R32G32_FLOAT:
            memcpy(out, src, 8);
            break;
        case PIPE_FORMAT_R32G32B32_FLOAT:
            memcpy(out, src, 12);
            break;
        case PIPE_FORMAT_R32G32B32A32_FLOAT:
            memcpy(out, src, 16);
            break;
        case PIPE_FORMAT_R8_UNORM:
            out[0] = src[0] / 255.0f;
            break;
        case PIPE_FORMAT_R8G8_UNORM:
            out[0] = src[0] / 255.0f;
            out[1] = src[1] / 255.0f;
            break;
        case PIPE_FORMAT_R8G8B8A8_UNORM:
            out[0] = src[0] / 255.0f;
            out[1] = src[1] / 255.0f;
            out[2] = src[2] / 255.0f;
            out[3] = src[3] / 255.0f;
            break;
        case PIPE_FORMAT_B8G8R8A8_UNORM:
            out[0] = src[2] / 255.0f;
            out[1] = src[1] / 255.0f;
            out[2] = src[0] / 255.0f;
            out[3] = src[3] / 255.0f;
            break;
        default:
            fprintf(stderr, "fetch_attrib: unhandled format %d\n", fmt);
            break;
    }
}

static void fetch_vertex(struct mypipe_context *mypipe, unsigned vert_idx,
                         float inputs[][4], unsigned *num_inputs){
    struct mp_vertex_element_state *ve = mypipe->velems;
    *num_inputs = ve->num_elements;

    for(unsigned a = 0; a < ve->num_elements; a++){
        struct pipe_vertex_element *elem = &ve->elements[a];
        unsigned buf_idx = elem->vertex_buffer_index;
        struct pipe_vertex_buffer *vb = &mypipe->vertex_buffers[buf_idx];

        const uint8_t *base;
        if (vb->is_user_buffer)
            base = (const uint8_t *)vb->buffer.user;
        else {
            struct mypipe_resource *res = mypipe_resource(vb->buffer.resource);
            base = (const uint8_t *)res->data;
        }

        unsigned offset = vb->buffer_offset + elem->src_offset + vert_idx * elem->src_stride;
        fetch_attrib(base + offset, elem->src_format, inputs[a]);
    }
}

/* ------------------------------------------------------------------ */
/* Index buffer reading                                                */
/* ------------------------------------------------------------------ */

static unsigned get_index(const struct pipe_draw_info *info,
                          const uint8_t *index_data, unsigned i) {
    switch (info->index_size) {
    case 1: return index_data[i];
    case 2: return ((const uint16_t *)index_data)[i];
    case 4: return ((const uint32_t *)index_data)[i];
    default: return i;
    }
}

/* ------------------------------------------------------------------ */
/* Sutherland-Hodgman clip against one plane                           */
/* ------------------------------------------------------------------ */

#define MP_MAX_CLIP_VERTS 12

struct mp_clip_vertex {
    float clip_pos[4];
    float varyings[MP_MAX_VARYINGS][4];
    unsigned num_varyings;
};

static float clip_dot(const float pos[4], int plane) {
    /* planes: 0: +w-x, 1: +w+x, 2: +w-y, 3: +w+y, 4: +w-z, 5: +w+z */
    int axis = plane >> 1;
    float val = pos[axis];
    float w = pos[3];
    if (plane & 1)
        return w + val;  /* -w <= val */
    else
        return w - val;  /* val <= w */
}

static void clip_lerp(const struct mp_clip_vertex *a, const struct mp_clip_vertex *b,
                      float t, struct mp_clip_vertex *out) {
    for (int i = 0; i < 4; i++)
        out->clip_pos[i] = a->clip_pos[i] + t * (b->clip_pos[i] - a->clip_pos[i]);
    out->num_varyings = a->num_varyings;
    for (unsigned v = 0; v < a->num_varyings; v++)
        for (int c = 0; c < 4; c++)
            out->varyings[v][c] = a->varyings[v][c] + t * (b->varyings[v][c] - a->varyings[v][c]);
}

static unsigned clip_polygon_plane(struct mp_clip_vertex *in, unsigned n_in,
                                   struct mp_clip_vertex *out, int plane) {
    if (n_in == 0) return 0;
    unsigned n_out = 0;

    for (unsigned i = 0; i < n_in; i++) {
        unsigned j = (i + 1) % n_in;
        float di = clip_dot(in[i].clip_pos, plane);
        float dj = clip_dot(in[j].clip_pos, plane);

        if (di >= 0) {
            /* i is inside */
            if (n_out < MP_MAX_CLIP_VERTS)
                out[n_out++] = in[i];
            if (dj < 0) {
                /* j is outside — emit intersection */
                float t = di / (di - dj);
                if (n_out < MP_MAX_CLIP_VERTS)
                    clip_lerp(&in[i], &in[j], t, &out[n_out++]);
            }
        } else {
            /* i is outside */
            if (dj >= 0) {
                /* j is inside — emit intersection */
                float t = di / (di - dj);
                if (n_out < MP_MAX_CLIP_VERTS)
                    clip_lerp(&in[i], &in[j], t, &out[n_out++]);
            }
        }
    }
    return n_out;
}

/* clip_triangle: Sutherland-Hodgman against frustum planes.
 * clip_z: if true, clip against near/far Z planes (planes 4,5).
 *         if false, only clip XY (planes 0-3).
 *         Matches draw module: clip_z = rasterizer->depth_clip_near. */
static unsigned clip_triangle(struct mp_clip_vertex *verts, bool clip_z) {
    struct mp_clip_vertex buf[MP_MAX_CLIP_VERTS];
    unsigned n = 3;
    int num_planes = clip_z ? 6 : 4;

    for (int plane = 0; plane < num_planes; plane++) {
        struct mp_clip_vertex *src = (plane & 1) ? buf : verts;
        struct mp_clip_vertex *dst = (plane & 1) ? verts : buf;
        n = clip_polygon_plane(src, n, dst, plane);
        if (n == 0) return 0;
    }
    /* With 6 planes (even count): result in verts.
     * With 4 planes (even count): result in verts. Good. */
    return n;
}

/* ------------------------------------------------------------------ */
/* Face culling                                                        */
/* ------------------------------------------------------------------ */

static bool mp_cull_triangle(const struct mypipe_context *ctx,
                             const struct mp_vertex *v0,
                             const struct mp_vertex *v1,
                             const struct mp_vertex *v2) {
    if (!ctx->rasterizer) return false;

    unsigned cull_face = ctx->rasterizer->cull_face;
    if (cull_face == PIPE_FACE_NONE) return false;

    /* Match draw module's sign convention (draw_pipe_cull.c):
     *   e = v0 - v2, f = v1 - v2
     *   det = e.x * f.y - e.y * f.x
     *   ccw = (det < 0)
     */
    float ex = v0->pos[0] - v2->pos[0];
    float ey = v0->pos[1] - v2->pos[1];
    float fx = v1->pos[0] - v2->pos[0];
    float fy = v1->pos[1] - v2->pos[1];
    float det = ex * fy - ey * fx;

    if (det == 0.0f) {
        /* Zero-area triangle is back-facing (per GL spec) */
        return (cull_face & PIPE_FACE_BACK) != 0;
    }

    unsigned ccw = (det < 0);
    unsigned face = (ccw == ctx->rasterizer->front_ccw)
                    ? PIPE_FACE_FRONT : PIPE_FACE_BACK;

    return (face & cull_face) != 0;
}

/* ------------------------------------------------------------------ */
/* Viewport transform                                                  */
/* ------------------------------------------------------------------ */

static void viewport_transform(const struct pipe_viewport_state *vp,
                                const float clip_pos[4],
                                const float vary[][4], unsigned num_vary,
                                struct mp_vertex *out) {
    float inv_w = 1.0f / clip_pos[3];
    float ndc[3] = {
        clip_pos[0] * inv_w,
        clip_pos[1] * inv_w,
        clip_pos[2] * inv_w
    };

    out->pos[0] = ndc[0] * vp->scale[0] + vp->translate[0];
    out->pos[1] = ndc[1] * vp->scale[1] + vp->translate[1];
    out->pos[2] = ndc[2] * vp->scale[2] + vp->translate[2];
    out->pos[3] = inv_w;

    out->num_varyings = num_vary;
    memcpy(out->varyings, vary, num_vary * 4 * sizeof(float));
}

/* ------------------------------------------------------------------ */
/* Emit helpers                                                        */
/* ------------------------------------------------------------------ */

static const void *resolve_uniforms(const struct pipe_constant_buffer *cbuf) {
    if (cbuf->user_buffer)
        return cbuf->user_buffer;
    if (cbuf->buffer) {
        struct mypipe_resource *mpr = mypipe_resource(cbuf->buffer);
        if (mpr->data)
            return (const uint8_t *)mpr->data + cbuf->buffer_offset;
    }
    return NULL;
}

static void emit_triangle(struct mypipe_context *mypipe,
                           unsigned i0, unsigned i1, unsigned i2,
                           const void *vs_uniforms,
                           struct mp_framebuffer *mpfb) {
    struct pipe_viewport_state *vp = &mypipe->viewport;
    float inputs[MP_MAX_ATTRIBS][4];
    unsigned num_inputs;

    /* Run VS for each vertex and store clip-space results */
    struct mp_clip_vertex clip_verts[MP_MAX_CLIP_VERTS];

    unsigned indices[3] = { i0, i1, i2 };
    for (unsigned v = 0; v < 3; v++) {
        fetch_vertex(mypipe, indices[v], inputs, &num_inputs);

        struct mp_vs_output vs_out;
        mp_run_vs(mypipe->vs, mypipe, (const float (*)[4])inputs, num_inputs,
                  vs_uniforms, &vs_out);

        memcpy(clip_verts[v].clip_pos, vs_out.position, 4 * sizeof(float));
        clip_verts[v].num_varyings = vs_out.num_varyings;
        memcpy(clip_verts[v].varyings, vs_out.varyings,
               vs_out.num_varyings * 4 * sizeof(float));
    }

    /* Save provoking vertex varyings BEFORE clipping (clipping creates new verts) */
    float pv_varyings[MP_MAX_VARYINGS][4];
    unsigned pv_num = 0;
    bool do_flatshade = mypipe->rasterizer && mypipe->rasterizer->flatshade;
    if (do_flatshade) {
        unsigned pv = mypipe->rasterizer->flatshade_first ? 0 : 2;
        pv_num = clip_verts[pv].num_varyings;
        memcpy(pv_varyings, clip_verts[pv].varyings, pv_num * 4 * sizeof(float));
    }

    /* Clip — only clip Z planes if depth_clip_near is set
     * (matches draw module: clip_z = rasterizer->depth_clip_near) */
    bool clip_z = mypipe->rasterizer && mypipe->rasterizer->depth_clip_near;
    unsigned n = clip_triangle(clip_verts, clip_z);
    if (n < 3) return;

    /* Flat shading: stamp provoking vertex's varyings onto all clipped verts */
    if (do_flatshade) {
        for (unsigned i = 0; i < n; i++)
            memcpy(clip_verts[i].varyings, pv_varyings, pv_num * 4 * sizeof(float));
    }

    /* Fan-triangulate the clipped polygon */
    for (unsigned i = 1; i + 1 < n; i++) {
        struct mp_vertex sv[3];
        viewport_transform(vp, clip_verts[0].clip_pos,
                           (const float (*)[4])clip_verts[0].varyings,
                           clip_verts[0].num_varyings, &sv[0]);
        viewport_transform(vp, clip_verts[i].clip_pos,
                           (const float (*)[4])clip_verts[i].varyings,
                           clip_verts[i].num_varyings, &sv[1]);
        viewport_transform(vp, clip_verts[i+1].clip_pos,
                           (const float (*)[4])clip_verts[i+1].varyings,
                           clip_verts[i+1].num_varyings, &sv[2]);

        if (mp_cull_triangle(mypipe, &sv[0], &sv[1], &sv[2]))
            continue;

        mp_rasterize_triangle(mypipe, &sv[0], &sv[1], &sv[2], mpfb);
    }
}

static void emit_line(struct mypipe_context *mypipe,
                      unsigned i0, unsigned i1,
                      const void *vs_uniforms,
                      struct mp_framebuffer *mpfb) {
    struct pipe_viewport_state *vp = &mypipe->viewport;
    float inputs[MP_MAX_ATTRIBS][4];
    unsigned num_inputs;

    struct mp_vertex sv[2];

    for (unsigned v = 0; v < 2; v++) {
        unsigned idx = (v == 0) ? i0 : i1;
        fetch_vertex(mypipe, idx, inputs, &num_inputs);

        struct mp_vs_output vs_out;
        mp_run_vs(mypipe->vs, mypipe, (const float (*)[4])inputs, num_inputs,
                  vs_uniforms, &vs_out);

        viewport_transform(vp, vs_out.position,
                           (const float (*)[4])vs_out.varyings,
                           vs_out.num_varyings, &sv[v]);
    }

    mp_rasterize_line(mypipe, &sv[0], &sv[1], mpfb);
}

static void emit_point(struct mypipe_context *mypipe,
                       unsigned i0,
                       const void *vs_uniforms,
                       struct mp_framebuffer *mpfb) {
    struct pipe_viewport_state *vp = &mypipe->viewport;
    float inputs[MP_MAX_ATTRIBS][4];
    unsigned num_inputs;

    fetch_vertex(mypipe, i0, inputs, &num_inputs);

    struct mp_vs_output vs_out;
    mp_run_vs(mypipe->vs, mypipe, (const float (*)[4])inputs, num_inputs,
              vs_uniforms, &vs_out);

    struct mp_vertex sv;
    viewport_transform(vp, vs_out.position,
                       (const float (*)[4])vs_out.varyings,
                       vs_out.num_varyings, &sv);
    sv.point_size = vs_out.point_size;

    mp_rasterize_point(mypipe, &sv, mpfb);
}

/* ------------------------------------------------------------------ */
/* Main draw entry point                                               */
/* ------------------------------------------------------------------ */

void mypipe_do_draw_vbo(struct mypipe_context *mypipe,
                        const struct pipe_draw_info *info,
                        const struct pipe_draw_start_count_bias *draws,
                        unsigned num_draws) {
    struct pipe_framebuffer_state *fb = &mypipe->framebuffer;
    if (fb->nr_cbufs == 0) return;

    struct pipe_surface *surf = &fb->cbufs[0];
    struct mypipe_resource *color_res = mypipe_resource(surf->texture);

    /* Use shadow buffer for rendering (avoids flickering on display targets) */
    struct mp_framebuffer mpfb;
    memset(&mpfb, 0, sizeof(mpfb));

    mpfb.color_map = color_res->data;
    if (!mpfb.color_map) return;

    mpfb.color_stride = color_res->stride[0];
    mpfb.width = fb->width;
    mpfb.height = fb->height;
    mpfb.color_format = surf->format;

    /* Map depth/stencil buffer if present */
    mpfb.depth_map = NULL;
    mpfb.stencil_map = NULL;
    mpfb.depth_stride = 0;
    struct mypipe_resource *zs_res = NULL;

    if (fb->zsbuf.texture) {
        zs_res = mypipe_resource(fb->zsbuf.texture);
        uint8_t *zs_map = zs_res->data;

        if (zs_map) {
            enum pipe_format zfmt = fb->zsbuf.texture->format;
            mpfb.depth_map = zs_map;
            mpfb.depth_stride = zs_res->stride[0]; /* bytes per row */
            if (zfmt == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
                zfmt == PIPE_FORMAT_S8_UINT_Z24_UNORM) {
                mpfb.stencil_map = zs_map; /* stencil is packed in same buffer */
            }
        }
    }

    /* Resolve uniforms */
    const void *vs_uniforms = resolve_uniforms(&mypipe->vs_cbuf);

    /* Resolve index buffer */
    const uint8_t *index_data = NULL;
    if (info->index_size > 0) {
        if (info->has_user_indices)
            index_data = (const uint8_t *)info->index.user;
        else if (info->index.resource) {
            struct mypipe_resource *ib_res = mypipe_resource(info->index.resource);
            index_data = (const uint8_t *)ib_res->data;
        }
    }

    for (unsigned d = 0; d < num_draws; d++) {
        unsigned start = draws[d].start;
        unsigned count = draws[d].count;
        if (count == 0) continue;

        /* Get actual vertex index for a given primitive vertex */
        #define VIDX(i) (index_data ? get_index(info, index_data, start + (i)) : (start + (i)))

        switch (info->mode) {
        case MESA_PRIM_TRIANGLES:
            for (unsigned i = 0; i + 2 < count; i += 3)
                emit_triangle(mypipe, VIDX(i), VIDX(i+1), VIDX(i+2), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_TRIANGLE_STRIP:
            for (unsigned i = 0; i + 2 < count; i++) {
                if (i & 1)
                    emit_triangle(mypipe, VIDX(i+1), VIDX(i), VIDX(i+2), vs_uniforms, &mpfb);
                else
                    emit_triangle(mypipe, VIDX(i), VIDX(i+1), VIDX(i+2), vs_uniforms, &mpfb);
            }
            break;

        case MESA_PRIM_TRIANGLE_FAN:
            for (unsigned i = 1; i + 1 < count; i++)
                emit_triangle(mypipe, VIDX(0), VIDX(i), VIDX(i+1), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_LINES:
            for (unsigned i = 0; i + 1 < count; i += 2)
                emit_line(mypipe, VIDX(i), VIDX(i+1), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_LINE_STRIP:
            for (unsigned i = 0; i + 1 < count; i++)
                emit_line(mypipe, VIDX(i), VIDX(i+1), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_LINE_LOOP:
            for (unsigned i = 0; i + 1 < count; i++)
                emit_line(mypipe, VIDX(i), VIDX(i+1), vs_uniforms, &mpfb);
            if (count > 1)
                emit_line(mypipe, VIDX(count-1), VIDX(0), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_POINTS:
            for (unsigned i = 0; i < count; i++)
                emit_point(mypipe, VIDX(i), vs_uniforms, &mpfb);
            break;

        case MESA_PRIM_QUADS:
            for (unsigned i = 0; i + 4 <= count; i += 4) {
                /* Match softpipe's decomposition: fan from last vertex (v3) */
                emit_triangle(mypipe, VIDX(i), VIDX(i+1), VIDX(i+3), vs_uniforms, &mpfb);
                emit_triangle(mypipe, VIDX(i+1), VIDX(i+2), VIDX(i+3), vs_uniforms, &mpfb);
            }
            break;

        case MESA_PRIM_QUAD_STRIP:
            for (unsigned i = 0; i + 4 <= count; i += 2) {
                /* Match softpipe: (v0,v1,v3), (v2,v0,v3) */
                emit_triangle(mypipe, VIDX(i), VIDX(i+1), VIDX(i+3), vs_uniforms, &mpfb);
                emit_triangle(mypipe, VIDX(i+2), VIDX(i), VIDX(i+3), vs_uniforms, &mpfb);
            }
            break;

        case MESA_PRIM_POLYGON:
            /* Same as triangle fan — vertex 0 is the hub */
            for (unsigned i = 1; i + 1 < count; i++)
                emit_triangle(mypipe, VIDX(0), VIDX(i), VIDX(i+1), vs_uniforms, &mpfb);
            break;

        default:
            fprintf(stderr, "mp_draw: unsupported mode %d\n", info->mode);
            break;
        }

        #undef VIDX
    }

    /* No unmap needed — we render to mpr->data shadow buffers */
}