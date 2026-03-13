# draw_vbo Pipeline Architecture

Quad-based rasterization with edge functions, modular per-fragment stages, and stub shaders (to be replaced by NIR interpreter).

---

## 1. Updated `mp_context.h`

```c
#ifndef MP_CONTEXT_H
#define MP_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#define MP_MAX_ATTRIBS 16

struct mp_vertex_element_state {
    unsigned num_elements;
    struct pipe_vertex_element elements[MP_MAX_ATTRIBS];
};

struct mp_compiled_shader {
    nir_shader *nir;
};

struct mypipe_context {
    struct pipe_context pipe;
    struct pipe_framebuffer_state framebuffer;

    /* Bound state */
    struct pipe_viewport_state viewport;
    struct pipe_vertex_buffer vertex_buffers[MP_MAX_ATTRIBS];
    unsigned num_vertex_buffers;
    struct mp_vertex_element_state *velems;

    /* Shaders */
    struct mp_compiled_shader *vs;
    struct mp_compiled_shader *fs;
};

static inline struct mypipe_context *mypipe_context(struct pipe_context *pipe) {
    return (struct mypipe_context *)pipe;
}

struct pipe_context *mypipe_create_context(struct pipe_screen *, void *priv, unsigned int flags);

#endif
```

---

## 2. Changes to `mp_state_shader.c`

Add `#include "mp_context.h"` and `#include "util/ralloc.h"` at the top.

**create_vs_state** — allocate `mp_compiled_shader`, take ownership of NIR:
```c
static void *mypipe_create_vs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ) {
    fprintf(stderr, "=== mypipe_create_vs_state ===\n");
    struct mp_compiled_shader *shader = CALLOC_STRUCT(mp_compiled_shader);
    if (templ->type == PIPE_SHADER_IR_NIR && templ->ir.nir) {
        shader->nir = templ->ir.nir;
        nir_print_shader(shader->nir, stderr);
    }
    return shader;
}
```

**create_fs_state** — same pattern:
```c
static void *mypipe_create_fs_state(struct pipe_context *pipe,
                                    const struct pipe_shader_state *templ) {
    fprintf(stderr, "=== mypipe_create_fs_state ===\n");
    struct mp_compiled_shader *shader = CALLOC_STRUCT(mp_compiled_shader);
    if (templ->type == PIPE_SHADER_IR_NIR && templ->ir.nir) {
        shader->nir = templ->ir.nir;
        nir_print_shader(shader->nir, stderr);
    }
    return shader;
}
```

**bind functions** — store in context:
```c
static void mypipe_bind_vs_state(struct pipe_context *pipe, void *vs) {
    mypipe_context(pipe)->vs = vs;
}

static void mypipe_bind_fs_state(struct pipe_context *pipe, void *fs) {
    mypipe_context(pipe)->fs = fs;
}
```

**delete functions** — free the struct:
```c
static void mypipe_delete_vs_state(struct pipe_context *pipe, void *vs) {
    struct mp_compiled_shader *shader = vs;
    if (shader->nir) ralloc_free(shader->nir);
    FREE(shader);
}

static void mypipe_delete_fs_state(struct pipe_context *pipe, void *fs) {
    struct mp_compiled_shader *shader = fs;
    if (shader->nir) ralloc_free(shader->nir);
    FREE(shader);
}
```

---

## 3. Changes to `mp_state_vertex.c`

Add `#include "mp_context.h"` and `#include "util/u_memory.h"`.

**create** — allocate and copy elements:
```c
static void *mypipe_create_vertex_elements_state(struct pipe_context *pipe,
                                                 unsigned num_elements,
                                                 const struct pipe_vertex_element *attribs) {
    struct mp_vertex_element_state *state = CALLOC_STRUCT(mp_vertex_element_state);
    state->num_elements = num_elements;
    memcpy(state->elements, attribs, num_elements * sizeof(*attribs));
    for (unsigned i = 0; i < num_elements; i++)
        fprintf(stderr, "  elem[%u]: offset=%u fmt=%d buf=%u stride=%u\n",
                i, attribs[i].src_offset, attribs[i].src_format,
                attribs[i].vertex_buffer_index, attribs[i].src_stride);
    return state;
}
```

**bind** — store pointer:
```c
static void mypipe_bind_vertex_elements_state(struct pipe_context *pipe, void *velems) {
    mypipe_context(pipe)->velems = velems;
}
```

**delete** — free:
```c
static void mypipe_delete_vertex_elements_state(struct pipe_context *pipe, void *velems) {
    FREE(velems);
}
```

**set_vertex_buffers** — copy into context:
```c
static void mypipe_set_vertex_buffers(struct pipe_context *pipe,
                                      unsigned count,
                                      const struct pipe_vertex_buffer *buffers) {
    struct mypipe_context *mypipe = mypipe_context(pipe);
    mypipe->num_vertex_buffers = count;
    if (buffers)
        memcpy(mypipe->vertex_buffers, buffers, count * sizeof(*buffers));
}
```

---

## 4. Changes to `mp_state_clip.c`

Add `#include "mp_context.h"`.

**set_viewport_states** — store first viewport:
```c
static void mypipe_set_viewport_states(struct pipe_context *pipe,
                                       unsigned start_slot, unsigned num_viewports,
                                       const struct pipe_viewport_state *viewport) {
    mypipe_context(pipe)->viewport = viewport[0];
    fprintf(stderr, "STUB: mypipe_set_viewport_states: scale=(%.1f,%.1f,%.1f) translate=(%.1f,%.1f,%.1f)\n",
            viewport[0].scale[0], viewport[0].scale[1], viewport[0].scale[2],
            viewport[0].translate[0], viewport[0].translate[1], viewport[0].translate[2]);
}
```

---

## 5. Changes to `mp_context.c`

Add `#include "mp_draw.h"` at the top. Replace the draw_vbo body:

```c
static void mypipe_draw_vbo(struct pipe_context *pipe,
                            const struct pipe_draw_info *info,
                            unsigned drawid_offset,
                            const struct pipe_draw_indirect_info *indirect,
                            const struct pipe_draw_start_count_bias *draws,
                            unsigned num_draws) {
    mypipe_do_draw_vbo(mypipe_context(pipe), info, draws, num_draws);
}
```

---

## 6. New file: `mp_draw.h`

```c
#ifndef MP_DRAW_H
#define MP_DRAW_H

struct mypipe_context;
struct pipe_draw_info;
struct pipe_draw_start_count_bias;

void mypipe_do_draw_vbo(struct mypipe_context *mypipe,
                        const struct pipe_draw_info *info,
                        const struct pipe_draw_start_count_bias *draws,
                        unsigned num_draws);

#endif
```

---

## 7. New file: `mp_draw.c`

```c
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

/* ------------------------------------------------------------ */
/* Vertex fetch                                                  */
/* ------------------------------------------------------------ */

static void fetch_attrib(const uint8_t *src, enum pipe_format fmt, float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (fmt) {
    case PIPE_FORMAT_R32_FLOAT:          memcpy(out, src, 4);  break;
    case PIPE_FORMAT_R32G32_FLOAT:       memcpy(out, src, 8);  break;
    case PIPE_FORMAT_R32G32B32_FLOAT:    memcpy(out, src, 12); break;
    case PIPE_FORMAT_R32G32B32A32_FLOAT: memcpy(out, src, 16); break;
    default:
        fprintf(stderr, "fetch_attrib: unhandled format %d\n", fmt);
        break;
    }
}

static void fetch_vertex(struct mypipe_context *mypipe, unsigned vert_idx,
                         float inputs[][4], unsigned *num_inputs) {
    struct mp_vertex_element_state *ve = mypipe->velems;
    *num_inputs = ve->num_elements;

    for (unsigned a = 0; a < ve->num_elements; a++) {
        struct pipe_vertex_element *elem = &ve->elements[a];
        unsigned buf_idx = elem->vertex_buffer_index;
        struct pipe_vertex_buffer *vb = &mypipe->vertex_buffers[buf_idx];
        struct mypipe_resource *res = mypipe_resource(vb->buffer.resource);

        unsigned offset = vb->buffer_offset
                        + elem->src_offset
                        + vert_idx * elem->src_stride;

        fetch_attrib((const uint8_t *)res->data + offset, elem->src_format, inputs[a]);
    }
}

/* ------------------------------------------------------------ */
/* Pipeline                                                      */
/* ------------------------------------------------------------ */

void mypipe_do_draw_vbo(struct mypipe_context *mypipe,
                        const struct pipe_draw_info *info,
                        const struct pipe_draw_start_count_bias *draws,
                        unsigned num_draws) {
    /* Only triangles for now */
    if (info->mode != MESA_PRIM_TRIANGLES) {
        fprintf(stderr, "mp_draw: unsupported mode %d\n", info->mode);
        return;
    }

    struct pipe_framebuffer_state *fb = &mypipe->framebuffer;
    if (fb->nr_cbufs == 0) return;

    struct pipe_surface *surf = &fb->cbufs[0];
    struct mypipe_resource *color_res = mypipe_resource(surf->texture);
    struct sw_winsys *winsys = mypipe_screen(mypipe->pipe.screen)->winsys;

    /* Map color buffer for the duration of the draw */
    struct mp_framebuffer mpfb;
    if (color_res->dt)
        mpfb.color_map = winsys->displaytarget_map(winsys, color_res->dt, PIPE_MAP_WRITE);
    else
        mpfb.color_map = color_res->data;

    if (!mpfb.color_map) return;

    mpfb.color_stride = color_res->stride[0];
    mpfb.width = fb->width;
    mpfb.height = fb->height;
    mpfb.color_format = surf->format;

    struct pipe_viewport_state *vp = &mypipe->viewport;

    for (unsigned d = 0; d < num_draws; d++) {
        unsigned start = draws[d].start;
        unsigned count = draws[d].count;

        for (unsigned i = 0; i + 2 < count; i += 3) {
            struct mp_vertex tri[3];

            for (unsigned v = 0; v < 3; v++) {
                float inputs[MP_MAX_ATTRIBS][4];
                unsigned num_inputs;
                fetch_vertex(mypipe, start + i + v, inputs, &num_inputs);

                /* Run vertex shader */
                struct mp_vs_output vs_out;
                mp_run_vs(mypipe->vs, inputs, num_inputs, &vs_out);

                /* Perspective divide */
                float inv_w = 1.0f / vs_out.position[3];
                float ndc[3] = {
                    vs_out.position[0] * inv_w,
                    vs_out.position[1] * inv_w,
                    vs_out.position[2] * inv_w
                };

                /* Viewport transform */
                tri[v].pos[0] = ndc[0] * vp->scale[0] + vp->translate[0];
                tri[v].pos[1] = ndc[1] * vp->scale[1] + vp->translate[1];
                tri[v].pos[2] = ndc[2] * vp->scale[2] + vp->translate[2];
                tri[v].pos[3] = inv_w;

                tri[v].num_varyings = vs_out.num_varyings;
                memcpy(tri[v].varyings, vs_out.varyings,
                       vs_out.num_varyings * 4 * sizeof(float));
            }

            /* TODO: clipping */

            mp_rasterize_triangle(mypipe, &tri[0], &tri[1], &tri[2], &mpfb);
        }
    }

    /* Unmap */
    if (color_res->dt)
        winsys->displaytarget_unmap(winsys, color_res->dt);
}
```

---

## 8. New file: `mp_raster.h`

```c
#ifndef MP_RASTER_H
#define MP_RASTER_H

#include "pipe/p_format.h"

#define MP_MAX_VARYINGS 16

struct mp_vertex {
    float pos[4];                           /* screen-space x,y,z + 1/w */
    float varyings[MP_MAX_VARYINGS][4];
    unsigned num_varyings;
};

/*
 * 2x2 fragment quad.  Pixel layout:
 *   0 = (x+0, y+0)   top-left
 *   1 = (x+1, y+0)   top-right
 *   2 = (x+0, y+1)   bottom-left
 *   3 = (x+1, y+1)   bottom-right
 */
struct mp_quad {
    unsigned x, y;
    uint8_t  mask;                          /* coverage bitmask */
    float    bary[4][3];                    /* barycentric [pixel][v0,v1,v2] */
    float    z[4];                          /* interpolated depth */
    float    varyings[MP_MAX_VARYINGS][4][4]; /* [varying][pixel][component] */
    float    color_out[4][4];               /* FS output [pixel][rgba] */
};

struct mp_framebuffer {
    uint8_t       *color_map;
    unsigned       color_stride;
    unsigned       width, height;
    enum pipe_format color_format;
};

struct mypipe_context;

void mp_rasterize_triangle(struct mypipe_context *mypipe,
                           const struct mp_vertex *v0,
                           const struct mp_vertex *v1,
                           const struct mp_vertex *v2,
                           struct mp_framebuffer *fb);

#endif
```

---

## 9. New file: `mp_raster.c`

```c
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "pipe/p_defines.h"
#include "util/u_math.h"

#include "mp_raster.h"
#include "mp_shader_exec.h"
#include "mp_context.h"

/* ------------------------------------------------------------ */
/* Edge function                                                 */
/* ------------------------------------------------------------ */

/*
 * Returns positive value if P is to the left of edge V0->V1 (CCW inside).
 * This is 2x the signed area of triangle V0-V1-P.
 */
static inline float edge_func(float v0x, float v0y,
                               float v1x, float v1y,
                               float px,  float py) {
    return (px - v0x) * (v1y - v0y) - (py - v0y) * (v1x - v0x);
}

/* ------------------------------------------------------------ */
/* Per-fragment operations (stubs)                               */
/* ------------------------------------------------------------ */

/* Scissor test -- stub: pass all */
static void scissor_test(struct mp_quad *quad, struct mypipe_context *mypipe) {
    (void)quad; (void)mypipe;
}

/* Depth test -- stub: pass all */
static void depth_test(struct mp_quad *quad, struct mypipe_context *mypipe) {
    (void)quad; (void)mypipe;
}

/* Stencil test -- stub: pass all */
static void stencil_test(struct mp_quad *quad, struct mypipe_context *mypipe) {
    (void)quad; (void)mypipe;
}

/* Blend -- stub: overwrite */
static void blend(struct mp_quad *quad, struct mypipe_context *mypipe) {
    (void)quad; (void)mypipe;
}

/* ------------------------------------------------------------ */
/* Pixel write                                                   */
/* ------------------------------------------------------------ */

static void write_pixel(struct mp_framebuffer *fb,
                        unsigned x, unsigned y,
                        const float color[4]) {
    if (x >= fb->width || y >= fb->height) return;

    uint8_t r = (uint8_t)(CLAMP(color[0], 0.0f, 1.0f) * 255.0f);
    uint8_t g = (uint8_t)(CLAMP(color[1], 0.0f, 1.0f) * 255.0f);
    uint8_t b = (uint8_t)(CLAMP(color[2], 0.0f, 1.0f) * 255.0f);
    uint8_t a = (uint8_t)(CLAMP(color[3], 0.0f, 1.0f) * 255.0f);

    uint8_t *pixel = fb->color_map + y * fb->color_stride + x * 4;

    switch (fb->color_format) {
    case PIPE_FORMAT_B8G8R8A8_UNORM:
        pixel[0] = b; pixel[1] = g; pixel[2] = r; pixel[3] = a;
        break;
    case PIPE_FORMAT_R8G8B8A8_UNORM:
        pixel[0] = r; pixel[1] = g; pixel[2] = b; pixel[3] = a;
        break;
    default:
        break;
    }
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
            quad.x = qx;
            quad.y = qy;
            quad.mask = 0;

            /* Evaluate edge functions at each of the 4 pixel centers.
             * FS runs on ALL 4 pixels (even uncovered) so derivatives work. */
            for (int p = 0; p < 4; p++) {
                float px = qx + dx[p] + 0.5f;
                float py = qy + dy[p] + 0.5f;

                float w0 = edge_func(v1->pos[0], v1->pos[1],
                                     v2->pos[0], v2->pos[1], px, py);
                float w1 = edge_func(v2->pos[0], v2->pos[1],
                                     v0->pos[0], v0->pos[1], px, py);
                float w2 = edge_func(v0->pos[0], v0->pos[1],
                                     v1->pos[0], v1->pos[1], px, py);

                /* Inside test -- works for both CW and CCW because
                 * w and area share the same sign. */
                bool inside;
                if (area > 0)
                    inside = (w0 >= 0 && w1 >= 0 && w2 >= 0);
                else
                    inside = (w0 <= 0 && w1 <= 0 && w2 <= 0);

                if (inside)
                    quad.mask |= (1 << p);

                /* Barycentric coords -- valid regardless of winding
                 * because sign of w cancels with sign of area. */
                quad.bary[p][0] = w0 * inv_area;
                quad.bary[p][1] = w1 * inv_area;
                quad.bary[p][2] = w2 * inv_area;

                /* Interpolate depth */
                quad.z[p] = quad.bary[p][0] * v0->pos[2]
                          + quad.bary[p][1] * v1->pos[2]
                          + quad.bary[p][2] * v2->pos[2];

                /* Interpolate varyings (affine -- add perspective correction later) */
                unsigned nv = v0->num_varyings;
                for (unsigned a = 0; a < nv; a++)
                    for (unsigned c = 0; c < 4; c++)
                        quad.varyings[a][p][c] =
                            quad.bary[p][0] * v0->varyings[a][c] +
                            quad.bary[p][1] * v1->varyings[a][c] +
                            quad.bary[p][2] * v2->varyings[a][c];
            }

            if (quad.mask == 0) continue;

            /* --- Per-fragment pipeline stages --- */

            scissor_test(&quad, mypipe);
            if (quad.mask == 0) continue;

            /* Fragment shader runs on full quad (for dFdx/dFdy) */
            mp_run_fs(mypipe->fs, &quad);

            depth_test(&quad, mypipe);
            if (quad.mask == 0) continue;

            stencil_test(&quad, mypipe);
            if (quad.mask == 0) continue;

            blend(&quad, mypipe);

            /* Write surviving pixels */
            for (int p = 0; p < 4; p++) {
                if (quad.mask & (1 << p))
                    write_pixel(fb, qx + dx[p], qy + dy[p], quad.color_out[p]);
            }
        }
    }
}
```

---

## 10. New file: `mp_shader_exec.h`

```c
#ifndef MP_SHADER_EXEC_H
#define MP_SHADER_EXEC_H

#include "mp_raster.h"

struct mp_compiled_shader;

struct mp_vs_output {
    float position[4];
    float varyings[MP_MAX_VARYINGS][4];
    unsigned num_varyings;
};

/* Run vertex shader on one vertex.
 * inputs:  array of fetched attributes [attrib][xyzw]
 * out:     filled with position + varyings */
void mp_run_vs(const struct mp_compiled_shader *shader,
               const float inputs[][4], unsigned num_inputs,
               struct mp_vs_output *out);

/* Run fragment shader on a 2x2 quad.
 * Reads quad->varyings, writes quad->color_out for all 4 pixels. */
void mp_run_fs(const struct mp_compiled_shader *shader,
               struct mp_quad *quad);

#endif
```

---

## 11. New file: `mp_shader_exec.c`

```c
#include <stdio.h>
#include <string.h>
#include "mp_shader_exec.h"
#include "mp_context.h"

/*
 * Stub VS:  gl_Position = vec4(a_position.xyz, 1.0)
 *
 * Replace this with NIR interpreter later.
 */
void mp_run_vs(const struct mp_compiled_shader *shader,
               const float inputs[][4], unsigned num_inputs,
               struct mp_vs_output *out) {
    out->position[0] = inputs[0][0];
    out->position[1] = inputs[0][1];
    out->position[2] = inputs[0][2];
    out->position[3] = 1.0f;
    out->num_varyings = 0;
}

/*
 * Stub FS:  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0)
 *
 * Replace this with NIR interpreter later.
 */
void mp_run_fs(const struct mp_compiled_shader *shader,
               struct mp_quad *quad) {
    for (int p = 0; p < 4; p++) {
        quad->color_out[p][0] = 1.0f;
        quad->color_out[p][1] = 0.0f;
        quad->color_out[p][2] = 0.0f;
        quad->color_out[p][3] = 1.0f;
    }
}
```

---

## 12. `meson.build` — add three new files

Add to your mypipe sources list:
```
'mp_draw.c',
'mp_raster.c',
'mp_shader_exec.c',
```

---

## Notes

- `elem->src_stride` is used in vertex fetch. If it doesn't compile, check `pipe_vertex_element` in `p_state.h`.
- `ralloc_free` is used to free NIR shaders since NIR uses ralloc allocation. Add `#include "util/ralloc.h"` in `mp_state_shader.c`.
- The `i + 2 < count` guard in the triangle loop prevents reading past the vertex array.
- Pipeline order: vertex fetch -> VS -> perspective divide -> viewport transform -> rasterize (edge function, 2x2 quads) -> scissor -> FS -> depth -> stencil -> blend -> pixel write.
