#include <stdio.h>
#include <stdbool.h>
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_pack_color.h"
#include "util/u_upload_mgr.h"

#include "mp_draw.h"
#include "mp_texture.h"
#include "mp_screen.h"
#include "mp_context.h"
#include "mp_state.h"

#include "frontend/sw_winsys.h"

static void mypipe_destroy(struct pipe_context *pipe){
    fprintf(stderr, "mypipe_destroy\n");
    struct mypipe_context *mypipe = mypipe_context(pipe);
    if (mypipe->blitter)
        util_blitter_destroy(mypipe->blitter);
    if (mypipe->pipe.stream_uploader)
        u_upload_destroy(mypipe->pipe.stream_uploader);
    pipe_resource_reference(&mypipe->vs_cbuf.buffer, NULL);
    pipe_resource_reference(&mypipe->fs_cbuf.buffer, NULL);
    FREE(mypipe);
}

static void mypipe_draw_vbo(struct pipe_context *pipe,
                            const struct pipe_draw_info *info,
                            unsigned drawid_offset,
                            const struct pipe_draw_indirect_info *indirect,
                            const struct pipe_draw_start_count_bias *draws,
                            unsigned num_draws){
    struct mypipe_context *ctx = mypipe_context(pipe);
    static int draw_num = 0;
    fprintf(stderr, "MP_DRAW[%d]: mode=%u start=%u count=%u idx_sz=%u inst=%u\n",
            draw_num, info->mode, draws[0].start, draws[0].count,
            info->index_size, info->instance_count);
    fprintf(stderr, "  FB: %ux%u nr_cbufs=%u\n",
            ctx->framebuffer.width, ctx->framebuffer.height,
            ctx->framebuffer.nr_cbufs);
    for (unsigned c = 0; c < ctx->framebuffer.nr_cbufs; c++) {
        if (ctx->framebuffer.cbufs[c].texture)
            fprintf(stderr, "  cbuf[%u]: fmt=%u tex=%p\n", c,
                    ctx->framebuffer.cbufs[c].format,
                    (void*)ctx->framebuffer.cbufs[c].texture);
    }
    for (unsigned s = 0; s < MP_MAX_SAMPLERS; s++) {
        struct pipe_sampler_view *v = ctx->sampler_views[MESA_SHADER_FRAGMENT][s];
        if (v && v->texture) {
            struct mypipe_resource *mpr = mypipe_resource(v->texture);
            fprintf(stderr, "  FS sampler[%u]: fmt=%u view_fmt=%u %ux%u data=%p dt=%p swz=%u/%u/%u/%u\n",
                    s, v->texture->format, v->format,
                    v->texture->width0, v->texture->height0,
                    mpr->data, (void*)mpr->dt,
                    v->swizzle_r, v->swizzle_g, v->swizzle_b, v->swizzle_a);
        }
    }
    if (ctx->blend)
        fprintf(stderr, "  blend: en=%u src=%u dst=%u\n",
                ctx->blend->rt[0].blend_enable,
                ctx->blend->rt[0].rgb_src_factor,
                ctx->blend->rt[0].rgb_dst_factor);
    draw_num++;
    mypipe_do_draw_vbo(ctx, info, draws, num_draws);
}

static void mypipe_clear(struct pipe_context *pipe,
                         unsigned buffers,
                         uint32_t color_clear_mask,
                         uint8_t stencil_clear_mask,
                         const struct pipe_scissor_state *scissor_state,
                         const union pipe_color_union *color,
                         double depth,
                         unsigned stencil){
    fprintf(stderr, "mypipe_clear: buffers=0x%x", buffers);
    if (color) fprintf(stderr, " color=(%.2f,%.2f,%.2f,%.2f)", color->f[0], color->f[1], color->f[2], color->f[3]);
    if (buffers & PIPE_CLEAR_DEPTH) fprintf(stderr, " depth=%.2f", depth);
    if (buffers & PIPE_CLEAR_STENCIL) fprintf(stderr, " stencil=%u", stencil);
    fprintf(stderr, "\n");

    struct mypipe_context *mypipe = mypipe_context(pipe);

    if(buffers & PIPE_CLEAR_COLOR){
        for(unsigned i = 0; i < mypipe->framebuffer.nr_cbufs; i++){
            if(buffers & (PIPE_CLEAR_COLOR0 << i)){
                struct pipe_surface *ps = &mypipe->framebuffer.cbufs[i];
                struct mypipe_resource *mpr = mypipe_resource(ps->texture);

                uint8_t *map = mpr->data;

                uint32_t pixel;
                util_pack_color_ub((uint8_t)(color->f[0] * 255),
                                   (uint8_t)(color->f[1] * 255),
                                   (uint8_t)(color->f[2] * 255),
                                   (uint8_t)(color->f[3] * 255),
                                   ps->format, (union util_color *)&pixel);
                unsigned stride = mpr->stride[0];

                for(unsigned y = 0; y < mypipe->framebuffer.height; y++){
                    uint32_t *row = (uint32_t *)(map + y * stride);
                    for(unsigned x = 0; x < mypipe->framebuffer.width; x++){
                        row[x] = pixel;
                    }
                }
            }
        }
    }

    /* Clear depth buffer */
    if ((buffers & PIPE_CLEAR_DEPTH) && mypipe->framebuffer.zsbuf.texture) {
        struct pipe_surface *zsbuf = &mypipe->framebuffer.zsbuf;
        struct mypipe_resource *zmpr = mypipe_resource(zsbuf->texture);

        uint8_t *map = zmpr->data;

        if (map) {
            unsigned w = mypipe->framebuffer.width;
            unsigned h = mypipe->framebuffer.height;
            enum pipe_format zfmt = zsbuf->texture->format;

            if (zfmt == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
                zfmt == PIPE_FORMAT_Z24X8_UNORM) {
                /* Z24S8/Z24X8: depth in lower 24 bits, stencil/pad in upper 8 */
                uint32_t z24 = (uint32_t)((double)0xFFFFFF * CLAMP(depth, 0.0, 1.0));
                unsigned stride = zmpr->stride[0];
                for (unsigned y = 0; y < h; y++) {
                    uint32_t *row = (uint32_t *)(map + y * stride);
                    for (unsigned x = 0; x < w; x++) {
                        if (buffers & PIPE_CLEAR_STENCIL)
                            row[x] = (z24 & 0x00FFFFFF) | ((stencil & 0xFF) << 24);
                        else
                            row[x] = (row[x] & 0xFF000000) | (z24 & 0x00FFFFFF);
                    }
                }
            } else if (zfmt == PIPE_FORMAT_S8_UINT_Z24_UNORM ||
                       zfmt == PIPE_FORMAT_X8Z24_UNORM) {
                /* S8Z24/X8Z24: stencil/pad in lower 8 bits, depth in upper 24 */
                uint32_t z24 = (uint32_t)((double)0xFFFFFF * CLAMP(depth, 0.0, 1.0));
                unsigned stride = zmpr->stride[0];
                for (unsigned y = 0; y < h; y++) {
                    uint32_t *row = (uint32_t *)(map + y * stride);
                    for (unsigned x = 0; x < w; x++) {
                        if (buffers & PIPE_CLEAR_STENCIL)
                            row[x] = ((z24 & 0x00FFFFFF) << 8) | (stencil & 0xFF);
                        else
                            row[x] = (row[x] & 0xFF) | ((z24 & 0x00FFFFFF) << 8);
                    }
                }
            } else if (zfmt == PIPE_FORMAT_Z32_FLOAT) {
                float zf = (float)depth;
                unsigned stride = zmpr->stride[0];
                for (unsigned y = 0; y < h; y++) {
                    float *row = (float *)(map + y * stride);
                    for (unsigned x = 0; x < w; x++)
                        row[x] = zf;
                }
            } else if (zfmt == PIPE_FORMAT_Z16_UNORM) {
                uint16_t z16 = (uint16_t)(CLAMP(depth, 0.0, 1.0) * 65535.0);
                unsigned stride = zmpr->stride[0];
                for (unsigned y = 0; y < h; y++) {
                    uint16_t *row = (uint16_t *)(map + y * stride);
                    for (unsigned x = 0; x < w; x++)
                        row[x] = z16;
                }
            }

        }
    }

    /* Clear stencil only (when not combined with depth clear above for Z24S8) */
    if ((buffers & PIPE_CLEAR_STENCIL) && !(buffers & PIPE_CLEAR_DEPTH) &&
        mypipe->framebuffer.zsbuf.texture) {
        struct pipe_surface *zsbuf = &mypipe->framebuffer.zsbuf;
        struct mypipe_resource *zmpr = mypipe_resource(zsbuf->texture);
        enum pipe_format zfmt = zsbuf->texture->format;

        if (zfmt == PIPE_FORMAT_Z24_UNORM_S8_UINT && zmpr->data) {
            uint8_t *map = zmpr->data;
            unsigned w = mypipe->framebuffer.width;
            unsigned h = mypipe->framebuffer.height;
            unsigned stride = zmpr->stride[0];
            for (unsigned y = 0; y < h; y++) {
                uint32_t *row = (uint32_t *)(map + y * stride);
                for (unsigned x = 0; x < w; x++)
                    row[x] = (row[x] & 0x00FFFFFF) | ((stencil & 0xFF) << 24);
            }
        }
    }
}

static void mypipe_flush(struct pipe_context *pipe,
                         struct pipe_fence_handle **fence,
                         unsigned flags){
    fprintf(stderr, "mypipe_flush\n");
    /* Software driver — everything is synchronous. But callers that pass
     * a non-NULL fence expect a valid handle back (e.g. eglSwapBuffers). */
    if (fence)
        *fence = (struct pipe_fence_handle *)(uintptr_t)1;
}

static void mypipe_set_framebuffer_state(struct pipe_context *pipe,
                                         const struct pipe_framebuffer_state *framebuffer){
    fprintf(stderr, "mypipe_set_framebuffer_state: %ux%u nr_cbufs=%u zsbuf_tex=%p\n",
            framebuffer->width, framebuffer->height,
            framebuffer->nr_cbufs, (void*)framebuffer->zsbuf.texture);
    struct mypipe_context *mypipe = mypipe_context(pipe);
    mypipe->framebuffer = *framebuffer;
    for (unsigned i = 0; i < framebuffer->nr_cbufs; i++) {
        const struct pipe_surface *s = &framebuffer->cbufs[i];
        if (s->texture)
            fprintf(stderr, "  cbuf[%u]: format=%d texture=%p\n", i, s->format, (void*)s->texture);
    }
}

static void mypipe_set_debug_callback(struct pipe_context *pipe,
                                      const struct util_debug_callback *cb){
}

static void mypipe_texture_barrier(struct pipe_context *pipe, unsigned flags){
}

static void mypipe_memory_barrier(struct pipe_context *pipe, unsigned flags){
}

static void mypipe_render_condition(struct pipe_context *pipe,
                                    struct pipe_query *query,
                                    bool condition,
                                    enum pipe_render_cond_flag mode){
}

struct pipe_context *mypipe_create_context(struct pipe_screen *screen, void *priv, unsigned int flags){
    fprintf(stderr, "mypipe_create_context\n");
    struct mypipe_screen *mp_screen = mypipe_screen(screen);
    struct mypipe_context *mypipe = CALLOC_STRUCT(mypipe_context);

    util_init_math();

    mypipe->pipe.screen = screen;
    mypipe->pipe.destroy = mypipe_destroy;
    mypipe->pipe.priv = priv;

    mypipe->pipe.draw_vbo = mypipe_draw_vbo;
    mypipe->pipe.clear = mypipe_clear;
    mypipe->pipe.flush = mypipe_flush;
    mypipe->pipe.set_framebuffer_state = mypipe_set_framebuffer_state;
    mypipe->pipe.set_debug_callback = mypipe_set_debug_callback;
    mypipe->pipe.texture_barrier = mypipe_texture_barrier;
    mypipe->pipe.memory_barrier = mypipe_memory_barrier;
    mypipe->pipe.render_condition = mypipe_render_condition;

    mypipe_init_blend_funcs(&mypipe->pipe);
    mypipe_init_clip_funcs(&mypipe->pipe);
    mypipe_init_rasterizer_funcs(&mypipe->pipe);
    mypipe_init_sampler_funcs(&mypipe->pipe);
    mypipe_init_shader_funcs(&mypipe->pipe);
    mypipe_init_streamout_funcs(&mypipe->pipe);
    mypipe_init_context_texture_funcs(&mypipe->pipe);
    mypipe_init_vertex_funcs(&mypipe->pipe);
    mypipe_init_image_funcs(&mypipe->pipe);
    mypipe_init_query_funcs(&mypipe->pipe);

    mypipe->pipe.stream_uploader = u_upload_create_default(&mypipe->pipe);
    mypipe->pipe.const_uploader = mypipe->pipe.stream_uploader;

    mypipe->blitter = util_blitter_create(&mypipe->pipe);
    if (!mypipe->blitter) {
        fprintf(stderr, "mypipe: failed to create blitter\n");
        mypipe_destroy(&mypipe->pipe);
        return NULL;
    }
    util_blitter_cache_all_shaders(mypipe->blitter);

    return &mypipe->pipe;
}
