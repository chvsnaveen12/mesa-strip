/*
 * replay.c — Replay softpipe trace through mypipe's ACTUAL vertex pipeline.
 *
 * Includes mp_draw.c directly so every function (fetch_attrib, clip_triangle,
 * viewport_transform, mp_cull_triangle, etc.) is the exact same code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Mesa / Gallium headers */
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "nir_serialize.h"

/* Mypipe headers */
#include "mp_context.h"
#include "mp_raster.h"
#include "mp_shader_exec.h"

/* ── Import mypipe's mp_draw.c directly ──
 * This gives us the EXACT fetch_attrib, fetch_vertex, clip_triangle,
 * clip_dot, clip_lerp, clip_polygon_plane, viewport_transform,
 * mp_cull_triangle, get_index, emit_triangle, etc.
 *
 * We suppress the mypipe_do_draw_vbo symbol to avoid duplicate with
 * the one in libmypipe.a by wrapping it. */
#define mypipe_do_draw_vbo mypipe_do_draw_vbo_UNUSED
#include "../mp_draw.c"
#undef mypipe_do_draw_vbo

/* ── Trace tags ── */
#define SP_TAG_DRAW_INPUT    0x44524157
#define SP_TAG_REF_OUTPUT    0x52454656
#define SP_TAG_DRAW_END      0x444F4E45
#define SP_TAG_EOF           0xFFFFFFFF

/* ── Read helpers ── */
static int rd(FILE *f, void *buf, size_t n) {
    return fread(buf, 1, n, f) == n ? 0 : -1;
}
static int rd32(FILE *f, uint32_t *v) { return rd(f, v, 4); }
static int rdf(FILE *f, float *v)     { return rd(f, v, 4); }

/* ── Per-draw state ── */
struct draw_record {
    uint32_t draw_num;
    uint32_t mode, start, count, index_size, instance_count;
    uint32_t num_elements;
    struct { uint32_t format, offset, stride, vbuf_idx; } elems[16];
    uint32_t num_vbufs;
    struct { uint32_t buf_offset; uint8_t *data; uint32_t size; } vbufs[16];
    uint8_t *idx_data; uint32_t idx_data_size;
    uint8_t *vs_nir_blob; uint32_t vs_nir_size;
    uint8_t *vs_uniforms; uint32_t vs_uniform_size;
    struct pipe_viewport_state viewport;
    uint32_t front_ccw, cull_face, flatshade, flatshade_first;
    uint32_t clip_halfz, depth_clip_near;
    uint32_t fb_w, fb_h;
    uint32_t ref_num_attribs, ref_tri_count, ref_total_floats;
    float *ref_data;
};

static void free_rec(struct draw_record *r) {
    for (unsigned i = 0; i < r->num_vbufs; i++) free(r->vbufs[i].data);
    free(r->idx_data); free(r->vs_nir_blob);
    free(r->vs_uniforms); free(r->ref_data);
}

static int read_rec(FILE *f, struct draw_record *r) {
    memset(r, 0, sizeof(*r));
    uint32_t tag;
    if (rd32(f, &tag)) return -1;
    if (tag == SP_TAG_EOF) return 1;
    if (tag != SP_TAG_DRAW_INPUT) return -1;
    rd32(f, &r->draw_num);
    rd32(f, &r->mode); rd32(f, &r->start); rd32(f, &r->count);
    rd32(f, &r->index_size); rd32(f, &r->instance_count);
    rd32(f, &r->num_elements);
    for (unsigned i = 0; i < r->num_elements && i < 16; i++) {
        rd32(f, &r->elems[i].format); rd32(f, &r->elems[i].offset);
        rd32(f, &r->elems[i].stride); rd32(f, &r->elems[i].vbuf_idx);
    }
    rd32(f, &r->num_vbufs);
    for (unsigned i = 0; i < r->num_vbufs && i < 16; i++) {
        rd32(f, &r->vbufs[i].buf_offset);
        uint32_t tmp; rd32(f, &tmp);
        rd32(f, &r->vbufs[i].size);
        if (r->vbufs[i].size > 0) {
            r->vbufs[i].data = malloc(r->vbufs[i].size);
            rd(f, r->vbufs[i].data, r->vbufs[i].size);
        }
    }
    rd32(f, &r->idx_data_size);
    if (r->idx_data_size > 0) {
        r->idx_data = malloc(r->idx_data_size);
        rd(f, r->idx_data, r->idx_data_size);
    }
    rd32(f, &r->vs_nir_size);
    if (r->vs_nir_size > 0) {
        r->vs_nir_blob = malloc(r->vs_nir_size);
        rd(f, r->vs_nir_blob, r->vs_nir_size);
    }
    rd32(f, &r->vs_uniform_size);
    if (r->vs_uniform_size > 0) {
        r->vs_uniforms = malloc(r->vs_uniform_size);
        rd(f, r->vs_uniforms, r->vs_uniform_size);
    }
    rdf(f, &r->viewport.scale[0]); rdf(f, &r->viewport.scale[1]); rdf(f, &r->viewport.scale[2]);
    rdf(f, &r->viewport.translate[0]); rdf(f, &r->viewport.translate[1]); rdf(f, &r->viewport.translate[2]);
    rd32(f, &r->front_ccw); rd32(f, &r->cull_face);
    rd32(f, &r->flatshade); rd32(f, &r->flatshade_first);
    rd32(f, &r->clip_halfz); rd32(f, &r->depth_clip_near);
    rd32(f, &r->fb_w); rd32(f, &r->fb_h);
    if (rd32(f, &tag) || tag != SP_TAG_REF_OUTPUT) return -1;
    rd32(f, &r->ref_num_attribs); rd32(f, &r->ref_tri_count);
    rd32(f, &r->ref_total_floats);
    if (r->ref_total_floats > 0) {
        r->ref_data = malloc(r->ref_total_floats * sizeof(float));
        rd(f, r->ref_data, r->ref_total_floats * sizeof(float));
    }
    if (rd32(f, &tag) || tag != SP_TAG_DRAW_END) return -1;
    return 0;
}

/* ── Build a minimal mypipe_context from the captured state ── */
static void build_context(const struct draw_record *r,
                           struct mypipe_context *ctx,
                           struct mp_vertex_element_state *ve,
                           struct pipe_rasterizer_state *rast) {
    memset(ctx, 0, sizeof(*ctx));
    memset(ve, 0, sizeof(*ve));
    memset(rast, 0, sizeof(*rast));

    /* Vertex elements */
    ve->num_elements = r->num_elements;
    for (unsigned i = 0; i < r->num_elements; i++) {
        ve->elements[i].src_format = r->elems[i].format;
        ve->elements[i].src_offset = r->elems[i].offset;
        ve->elements[i].src_stride = r->elems[i].stride;
        ve->elements[i].vertex_buffer_index = r->elems[i].vbuf_idx;
    }
    ctx->velems = ve;

    /* Vertex buffers — point to captured data as user buffers */
    for (unsigned i = 0; i < r->num_vbufs; i++) {
        ctx->vertex_buffers[i].buffer_offset = r->vbufs[i].buf_offset;
        ctx->vertex_buffers[i].is_user_buffer = true;
        ctx->vertex_buffers[i].buffer.user = r->vbufs[i].data;
    }

    /* Viewport */
    ctx->viewport = r->viewport;

    /* Rasterizer */
    rast->front_ccw = r->front_ccw;
    rast->cull_face = r->cull_face;
    rast->flatshade = r->flatshade;
    rast->flatshade_first = r->flatshade_first;
    rast->depth_clip_near = r->depth_clip_near;
    ctx->rasterizer = rast;
}

/* ── Output triangle ── */
struct out_tri {
    float pos[3][4];
    float vary[3][MP_MAX_VARYINGS][4];
    unsigned num_vary;
};

/* ── Process one triangle through mypipe's REAL pipeline ──
 * Uses the actual fetch_vertex, mp_run_vs, clip_triangle,
 * viewport_transform, mp_cull_triangle from mp_draw.c */
static int process_triangle(struct mypipe_context *ctx,
                             struct mp_compiled_shader *vs,
                             const void *vs_uniforms,
                             unsigned i0, unsigned i1, unsigned i2,
                             struct out_tri *out, unsigned *out_count,
                             unsigned max_out) {
    float inputs[MP_MAX_ATTRIBS][4];
    unsigned num_inputs;
    struct mp_clip_vertex cv[MP_MAX_CLIP_VERTS];

    unsigned indices[3] = { i0, i1, i2 };
    for (int v = 0; v < 3; v++) {
        /* mypipe's actual fetch_vertex */
        fetch_vertex(ctx, indices[v], inputs, &num_inputs);

        /* mypipe's actual VS interpreter */
        struct mp_vs_output vs_out;
        mp_run_vs(vs, ctx, (const float (*)[4])inputs, num_inputs,
                  vs_uniforms, &vs_out);

        memcpy(cv[v].clip_pos, vs_out.position, 16);
        cv[v].num_varyings = vs_out.num_varyings;
        memcpy(cv[v].varyings, vs_out.varyings, vs_out.num_varyings * 16);
    }

    /* mypipe's actual clip_triangle */
    bool clip_z = ctx->rasterizer && ctx->rasterizer->depth_clip_near;
    unsigned n = clip_triangle(cv, clip_z);
    if (n < 3) return 0;

    /* Fan-triangulate + cull using mypipe's actual code */
    int emitted = 0;
    for (unsigned i = 1; i + 1 < n && *out_count < max_out; i++) {
        struct mp_vertex sv[3];
        unsigned fan[3] = { 0, i, i + 1 };

        /* mypipe's actual viewport_transform */
        for (int v = 0; v < 3; v++)
            viewport_transform(&ctx->viewport, cv[fan[v]].clip_pos,
                               (const float (*)[4])cv[fan[v]].varyings,
                               cv[fan[v]].num_varyings, &sv[v]);

        /* mypipe's actual mp_cull_triangle */
        if (mp_cull_triangle(ctx, &sv[0], &sv[1], &sv[2]))
            continue;

        struct out_tri *t = &out[*out_count];
        for (int v = 0; v < 3; v++) {
            memcpy(t->pos[v], sv[v].pos, 16);
            t->num_vary = sv[v].num_varyings;
            memcpy(t->vary[v], sv[v].varyings, sv[v].num_varyings * 16);
        }
        (*out_count)++;
        emitted++;
    }
    return emitted;
}

/* ── Compare floats ── */
static bool feq(float a, float b) {
    return fabsf(a - b) < 0.01f;
}

static const char *prim_name(unsigned m) {
    static const char *n[] = {"PTS","LINES","LLOOP","LSTRIP","TRIS","TSTRIP","TFAN","QUADS","QSTRIP","POLY"};
    return m < 10 ? n[m] : "?";
}

/* ── Main ── */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <trace.bin> [max_draws] [draw_id]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    unsigned max_draws = argc > 2 ? atoi(argv[2]) : ~0u;
    int only = argc > 3 ? atoi(argv[3]) : -1;

    char magic[8]; uint32_t hdr;
    rd(f, magic, 8); rd32(f, &hdr);
    if (memcmp(magic, "SPTRACE1", 8)) { fprintf(stderr, "Bad magic\n"); return 1; }

    glsl_type_singleton_init_or_ref();

    printf("=== Mypipe Trace Replay ===\n\n");

    unsigned n = 0, mismatches = 0, matches = 0, skipped = 0;

    while (n < max_draws) {
        struct draw_record r;
        if (read_rec(f, &r)) break;

        bool show = (only < 0 && n < 20) || only == (int)n;

        /* Compile VS */
        struct mp_compiled_shader vs;
        memset(&vs, 0, sizeof(vs));
        bool have_vs = false;
        if (r.vs_nir_size > 0) {
            static const nir_shader_compiler_options nir_opts = { 0 };
            struct blob_reader reader;
            blob_reader_init(&reader, r.vs_nir_blob, r.vs_nir_size);
            vs.nir = nir_deserialize(NULL, &nir_opts, &reader);
            if (vs.nir) {
                mp_lower_and_compile(&vs);
                have_vs = true;
            }
        }

        if (!have_vs || r.count == 0 || r.ref_tri_count == 0) {
            skipped++;
            if (vs.nir) ralloc_free(vs.nir);
            free_rec(&r); n++; continue;
        }

        /* Build mypipe context from captured state */
        struct mypipe_context ctx;
        struct mp_vertex_element_state ve;
        struct pipe_rasterizer_state rast;
        build_context(&r, &ctx, &ve, &rast);
        ctx.vs = &vs;

        /* Run mypipe's vertex pipeline */
        struct out_tri *my_tris = calloc(r.ref_tri_count * 2 + 64, sizeof(struct out_tri));
        unsigned my_tri_count = 0;

        #define VIDX(i) (r.index_size ? get_index(&(struct pipe_draw_info){.index_size=r.index_size}, r.idx_data, r.start+(i)) : r.start+(i))
        #define MAXOUT (r.ref_tri_count * 2 + 64)

        switch (r.mode) {
        case 4: /* TRIANGLES */
            for (unsigned i = 0; i + 2 < r.count; i += 3)
                process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(i), VIDX(i+1), VIDX(i+2), my_tris, &my_tri_count, MAXOUT);
            break;
        case 5: /* TRI_STRIP */
            for (unsigned i = 0; i + 2 < r.count; i++) {
                if (i & 1)
                    process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(i+1), VIDX(i), VIDX(i+2), my_tris, &my_tri_count, MAXOUT);
                else
                    process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(i), VIDX(i+1), VIDX(i+2), my_tris, &my_tri_count, MAXOUT);
            }
            break;
        case 6: /* TRI_FAN */
            for (unsigned i = 1; i + 1 < r.count; i++)
                process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(0), VIDX(i), VIDX(i+1), my_tris, &my_tri_count, MAXOUT);
            break;
        case 7: /* QUADS */
            for (unsigned i = 0; i + 4 <= r.count; i += 4) {
                process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(i), VIDX(i+1), VIDX(i+3), my_tris, &my_tri_count, MAXOUT);
                process_triangle(&ctx, &vs, r.vs_uniforms, VIDX(i+1), VIDX(i+2), VIDX(i+3), my_tris, &my_tri_count, MAXOUT);
            }
            break;
        default:
            break;
        }
        #undef VIDX
        #undef MAXOUT

        /* Compare: unordered vertex set per triangle */
        bool tri_count_match = (my_tri_count == r.ref_tri_count);
        bool pos_match = tri_count_match;
        unsigned first_mismatch_tri = ~0u;

        if (tri_count_match) {
            unsigned na = r.ref_num_attribs;
            unsigned fpv = na * 4;
            unsigned fpt = 3 * fpv;
            for (unsigned t = 0; t < r.ref_tri_count && pos_match; t++) {
                float *ref = r.ref_data + t * fpt;
                bool used[3] = {false, false, false};
                for (int rv = 0; rv < 3; rv++) {
                    float *rp = ref + rv * fpv;
                    bool found = false;
                    for (int mv = 0; mv < 3; mv++) {
                        if (used[mv]) continue;
                        float *mp = my_tris[t].pos[mv];
                        if (feq(rp[0],mp[0]) && feq(rp[1],mp[1]) && feq(rp[2],mp[2]) && feq(rp[3],mp[3])) {
                            used[mv] = true; found = true; break;
                        }
                    }
                    if (!found) { pos_match = false; first_mismatch_tri = t; break; }
                }
            }
        }

        if (pos_match && tri_count_match) matches++;
        else mismatches++;

        if (show || !pos_match || !tri_count_match) {
            printf("Draw #%u [%s count=%u]: ", n, prim_name(r.mode), r.count);
            if (!tri_count_match) {
                printf("TRI COUNT MISMATCH: softpipe=%u mypipe=%u\n", r.ref_tri_count, my_tri_count);
            } else if (!pos_match) {
                unsigned na = r.ref_num_attribs;
                unsigned fpv = na * 4;
                unsigned fpt = 3 * fpv;
                printf("POS MISMATCH at tri[%u] (%u tris total)\n", first_mismatch_tri, r.ref_tri_count);
                unsigned t_start = first_mismatch_tri > 0 ? first_mismatch_tri - 1 : 0;
                unsigned t_end = first_mismatch_tri + 4;
                if (t_end > r.ref_tri_count) t_end = r.ref_tri_count;
                if (t_end > my_tri_count) t_end = my_tri_count;
                for (unsigned t = t_start; t < t_end; t++) {
                    float *ref = r.ref_data + t * fpt;
                    printf("  tri[%u]\n", t);
                    for (int v = 0; v < 3; v++) {
                        float *rp = ref + v * fpv;
                        float *mp = my_tris[t].pos[v];
                        bool vm = feq(rp[0],mp[0]) && feq(rp[1],mp[1]);
                        printf("    v%d sp=(%.2f,%.2f) mp=(%.2f,%.2f)%s\n",
                               v, rp[0], rp[1], mp[0], mp[1], vm ? "" : "  <<<");
                    }
                }
            } else {
                printf("MATCH (%u tris)\n", r.ref_tri_count);
            }
        }

        free(my_tris);
        if (vs.nir) ralloc_free(vs.nir);
        free_rec(&r); n++;
    }

    printf("\n=== Results: %u draws, %u match, %u mismatch, %u skipped ===\n",
           n, matches, mismatches, skipped);
    fclose(f);
    return mismatches > 0 ? 1 : 0;
}
