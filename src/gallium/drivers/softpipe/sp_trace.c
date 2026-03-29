/*
 * sp_trace.c — Binary trace capture for softpipe draw_vbo calls.
 *
 * Captures input state and reference output triangles (screen-space vertices
 * as received by sp_setup_tri after the draw module has done VS, clip, and
 * viewport transform).
 */

#include <stdlib.h>
#include <string.h>

#include "sp_trace.h"
#include "sp_context.h"
#include "sp_state.h"
#include "sp_texture.h"

#include "pipe/p_state.h"
#include "nir_serialize.h"
#include "util/format/u_format.h"

/* ── helpers ── */

static void wr(FILE *fp, const void *data, size_t n) {
    fwrite(data, 1, n, fp);
}
static void wr32(FILE *fp, uint32_t v)  { wr(fp, &v, 4); }
static void wrf(FILE *fp, float v)      { wr(fp, &v, 4); }

/* ── init / fini ── */

void sp_trace_init(struct softpipe_context *sp) {
    memset(&sp->trace, 0, sizeof(sp->trace));
    const char *path = getenv("SP_TRACE");
    if (!path || !path[0])
        return;
    sp->trace.fp = fopen(path, "wb");
    if (!sp->trace.fp) {
        fprintf(stderr, "sp_trace: cannot open %s\n", path);
        return;
    }
    fprintf(stderr, "sp_trace: writing to %s\n", path);
    wr(sp->trace.fp, SP_TRACE_MAGIC, SP_TRACE_MAGIC_SZ);
    wr32(sp->trace.fp, 0); /* placeholder for draw count */
}

void sp_trace_fini(struct softpipe_context *sp) {
    if (!sp->trace.fp)
        return;
    wr32(sp->trace.fp, SP_TAG_EOF);
    /* patch draw count */
    fseek(sp->trace.fp, SP_TRACE_MAGIC_SZ, SEEK_SET);
    wr32(sp->trace.fp, sp->trace.draw_num);
    fclose(sp->trace.fp);
    sp->trace.fp = NULL;
    free(sp->trace.tri_buf);
    fprintf(stderr, "sp_trace: %u draw calls captured\n", sp->trace.draw_num);
}

/* ── capture inputs (called BEFORE draw_vbo) ── */

void sp_trace_capture_inputs(struct softpipe_context *sp,
                             const struct pipe_draw_info *info,
                             const struct pipe_draw_start_count_bias *draw) {
    FILE *fp = sp->trace.fp;
    if (!fp) return;

    wr32(fp, SP_TAG_DRAW_INPUT);
    wr32(fp, sp->trace.draw_num);

    /* draw parameters */
    wr32(fp, info->mode);
    wr32(fp, draw->start);
    wr32(fp, draw->count);
    wr32(fp, info->index_size);
    wr32(fp, info->instance_count);

    /* vertex elements */
    struct sp_velems_state *ve = sp->velems;
    uint32_t num_elements = ve ? ve->count : 0;
    wr32(fp, num_elements);
    for (unsigned i = 0; i < num_elements; i++) {
        struct pipe_vertex_element *e = &ve->velem[i];
        wr32(fp, e->src_format);
        wr32(fp, e->src_offset);
        wr32(fp, e->src_stride);
        wr32(fp, e->vertex_buffer_index);
    }

    /* vertex buffers */
    wr32(fp, sp->num_vertex_buffers);
    for (unsigned i = 0; i < sp->num_vertex_buffers; i++) {
        struct pipe_vertex_buffer *vb = &sp->vertex_buffer[i];
        wr32(fp, vb->buffer_offset);
        wr32(fp, vb->is_user_buffer);

        const void *ptr = NULL;
        uint32_t data_size = 0;
        if (vb->is_user_buffer) {
            ptr = vb->buffer.user;
            unsigned max_stride = 0, max_end = 0;
            for (unsigned e = 0; e < num_elements; e++) {
                if (ve->velem[e].vertex_buffer_index == i) {
                    unsigned s = ve->velem[e].src_stride;
                    if (s > max_stride) max_stride = s;
                    unsigned off = ve->velem[e].src_offset +
                                   util_format_get_blocksize(ve->velem[e].src_format);
                    if (off > max_end) max_end = off;
                }
            }
            unsigned last_vert = draw->start + draw->count;
            data_size = vb->buffer_offset + last_vert * max_stride + max_end;
        } else if (vb->buffer.resource) {
            ptr = softpipe_resource_data(vb->buffer.resource);
            data_size = vb->buffer.resource->width0;
        }
        wr32(fp, data_size);
        if (ptr && data_size > 0)
            wr(fp, ptr, data_size);
    }

    /* index buffer */
    if (info->index_size) {
        const void *idx_ptr = NULL;
        uint32_t idx_data_size = 0;
        if (info->has_user_indices) {
            idx_ptr = info->index.user;
            idx_data_size = (draw->start + draw->count) * info->index_size;
        } else if (info->index.resource) {
            idx_ptr = softpipe_resource_data(info->index.resource);
            idx_data_size = info->index.resource->width0;
        }
        wr32(fp, idx_data_size);
        if (idx_ptr && idx_data_size > 0)
            wr(fp, idx_ptr, idx_data_size);
    } else {
        wr32(fp, 0);
    }

    /* VS NIR (serialized at shader creation time, before nir_to_tgsi) */
    if (sp->vs && sp->vs->nir_blob_data && sp->vs->nir_blob_size > 0) {
        wr32(fp, (uint32_t)sp->vs->nir_blob_size);
        wr(fp, sp->vs->nir_blob_data, sp->vs->nir_blob_size);
    } else {
        wr32(fp, 0);
    }

    /* VS uniforms */
    if (sp->constants[MESA_SHADER_VERTEX][0]) {
        uint32_t sz = sp->constants[MESA_SHADER_VERTEX][0]->width0;
        const void *data = softpipe_resource_data(sp->constants[MESA_SHADER_VERTEX][0]);
        wr32(fp, sz);
        if (data && sz > 0)
            wr(fp, data, sz);
    } else {
        wr32(fp, 0);
    }

    /* viewport */
    struct pipe_viewport_state *vp = &sp->viewports[0];
    wrf(fp, vp->scale[0]);
    wrf(fp, vp->scale[1]);
    wrf(fp, vp->scale[2]);
    wrf(fp, vp->translate[0]);
    wrf(fp, vp->translate[1]);
    wrf(fp, vp->translate[2]);

    /* rasterizer state */
    if (sp->rasterizer) {
        wr32(fp, sp->rasterizer->front_ccw);
        wr32(fp, sp->rasterizer->cull_face);
        wr32(fp, sp->rasterizer->flatshade);
        wr32(fp, sp->rasterizer->flatshade_first);
        wr32(fp, sp->rasterizer->clip_halfz);
        wr32(fp, sp->rasterizer->depth_clip_near);
    } else {
        for (int i = 0; i < 6; i++) wr32(fp, 0);
    }

    /* framebuffer dims */
    wr32(fp, sp->framebuffer.width);
    wr32(fp, sp->framebuffer.height);

    /* Reset triangle accumulator */
    sp->trace.tri_buf_used = 0;
    sp->trace.tri_count = 0;
}

/* ── stash a triangle from sp_setup_tri ── */

void sp_trace_stash_triangle(struct softpipe_context *sp,
                             const float (*v0)[4],
                             const float (*v1)[4],
                             const float (*v2)[4]) {
    if (!sp->trace.fp) return;

    unsigned na = sp->vertex_info.num_attribs;
    if (na == 0) return;

    unsigned floats_per_tri = 3 * na * 4; /* 3 verts * na attribs * 4 components */
    unsigned needed = sp->trace.tri_buf_used + floats_per_tri;

    /* Grow buffer if needed */
    if (needed > sp->trace.tri_buf_cap) {
        unsigned new_cap = sp->trace.tri_buf_cap ? sp->trace.tri_buf_cap * 2 : 4096;
        while (new_cap < needed) new_cap *= 2;
        sp->trace.tri_buf = realloc(sp->trace.tri_buf, new_cap * sizeof(float));
        sp->trace.tri_buf_cap = new_cap;
    }

    /* Copy the three vertices */
    float *dst = sp->trace.tri_buf + sp->trace.tri_buf_used;
    const float (*verts[3])[4] = { v0, v1, v2 };
    for (int v = 0; v < 3; v++) {
        for (unsigned a = 0; a < na; a++) {
            dst[0] = verts[v][a][0];
            dst[1] = verts[v][a][1];
            dst[2] = verts[v][a][2];
            dst[3] = verts[v][a][3];
            dst += 4;
        }
    }
    sp->trace.tri_buf_used += floats_per_tri;
    sp->trace.tri_count++;
}

/* ── capture outputs (called AFTER draw_vbo + flush) ── */

void sp_trace_capture_outputs(struct softpipe_context *sp) {
    FILE *fp = sp->trace.fp;
    if (!fp) return;

    unsigned na = sp->vertex_info.num_attribs;

    wr32(fp, SP_TAG_REF_OUTPUT);
    wr32(fp, na);
    wr32(fp, sp->trace.tri_count);

    /* Write all accumulated triangle vertices as raw floats */
    unsigned total_floats = sp->trace.tri_buf_used;
    wr32(fp, total_floats);
    if (total_floats > 0)
        wr(fp, sp->trace.tri_buf, total_floats * sizeof(float));

    wr32(fp, SP_TAG_DRAW_END);
    sp->trace.draw_num++;

    fflush(fp);
}
