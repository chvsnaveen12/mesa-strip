#ifndef MP_RASTER_H
#define MP_RASTER_H

#include "pipe/p_defines.h"
#include "util/format/u_format.h"

#define MP_MAX_VARYINGS 16

struct mp_vertex {
    float pos[4];                           /* screen-space x,y,z + 1/w */
    float varyings[MP_MAX_VARYINGS][4];
    unsigned num_varyings;
    float point_size;                       /* gl_PointSize from VS */
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
    float    frag_coord[4][4];              /* gl_FragCoord [pixel][xyzw] */
    bool     front_face;                    /* gl_FrontFacing */
    bool     discard[4];                    /* per-pixel discard flag */
};

struct mp_framebuffer {
    uint8_t       *color_map;
    unsigned       color_stride;
    unsigned       width, height;
    enum pipe_format color_format;
    uint8_t       *depth_map;               /* NULL if no depth buffer (raw bytes) */
    uint8_t       *stencil_map;             /* NULL if no stencil buffer */
    unsigned       depth_stride;            /* in bytes */
};

struct mypipe_context;

void mp_rasterize_triangle(struct mypipe_context *mypipe,
                           const struct mp_vertex *v0,
                           const struct mp_vertex *v1,
                           const struct mp_vertex *v2,
                           struct mp_framebuffer *fb);

void mp_rasterize_line(struct mypipe_context *mypipe,
                       const struct mp_vertex *v0,
                       const struct mp_vertex *v1,
                       struct mp_framebuffer *fb);

void mp_rasterize_point(struct mypipe_context *mypipe,
                        const struct mp_vertex *v,
                        struct mp_framebuffer *fb);

#endif
