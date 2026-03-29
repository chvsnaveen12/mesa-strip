/*
 * sp_trace.h — Binary trace capture for draw_vbo calls.
 *
 * Enable: SP_TRACE=/path/to/output.bin
 *
 * Captures draw_vbo inputs and reference output triangles
 * (screen-space vertices from sp_setup_tri).
 */

#ifndef SP_TRACE_H
#define SP_TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include "sp_context.h"

struct pipe_draw_info;
struct pipe_draw_start_count_bias;

/* ── Trace file format constants ── */
#define SP_TRACE_MAGIC     "SPTRACE1"
#define SP_TRACE_MAGIC_SZ  8

#define SP_TAG_DRAW_INPUT    0x44524157  /* "DRAW" */
#define SP_TAG_REF_OUTPUT    0x52454656  /* "REFV" */
#define SP_TAG_DRAW_END      0x444F4E45  /* "DONE" */
#define SP_TAG_EOF           0xFFFFFFFF

/* ── API ── */

void sp_trace_init(struct softpipe_context *sp);
void sp_trace_fini(struct softpipe_context *sp);

void sp_trace_capture_inputs(struct softpipe_context *sp,
                             const struct pipe_draw_info *info,
                             const struct pipe_draw_start_count_bias *draw);

void sp_trace_capture_outputs(struct softpipe_context *sp);

/* Called from sp_setup_tri to accumulate reference triangles */
void sp_trace_stash_triangle(struct softpipe_context *sp,
                             const float (*v0)[4],
                             const float (*v1)[4],
                             const float (*v2)[4]);

static inline bool
sp_trace_active(const struct softpipe_context *sp) {
    return sp->trace.fp != NULL;
}

#endif /* SP_TRACE_H */
