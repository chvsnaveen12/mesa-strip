/* Stub - geometry shaders not supported in mesa-strip.
 * Provides struct definitions and no-op inline stubs so that code
 * referencing GS types/functions still compiles.
 */
#ifndef DRAW_GS_H
#define DRAW_GS_H

#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "tgsi/tgsi_scan.h"
#include "draw_context.h"

struct draw_context;
struct draw_vertex_info;
struct draw_prim_info;
struct draw_buffer_info;

/**
 * Minimal struct draw_vertex_stream (needed by draw_geometry_shader).
 */
struct draw_vertex_stream {
   unsigned *primitive_lengths;
   unsigned emitted_vertices;
   unsigned emitted_primitives;
   float (*tmp_output)[4];
};

/**
 * Minimal struct draw_geometry_shader -- just enough members so that
 * code dereferencing gs->info, gs->output_primitive, gs->viewport_index_output,
 * gs->ccdistance_output[], gs->num_vertex_streams, gs->state etc. compiles.
 */
struct draw_geometry_shader {
   struct draw_context *draw;

   struct pipe_shader_state state;
   struct tgsi_shader_info info;

   unsigned position_output;
   unsigned viewport_index_output;
   unsigned clipvertex_output;
   unsigned ccdistance_output[PIPE_MAX_CLIP_OR_CULL_DISTANCE_ELEMENT_COUNT];

   unsigned max_output_vertices;
   enum mesa_prim input_primitive;
   enum mesa_prim output_primitive;
   unsigned vertex_size;

   unsigned num_vertex_streams;
   unsigned num_invocations;
};

/*
 * Stub function: called from draw_new_instance().
 * No-op when GS is not supported.
 */
static inline void
draw_geometry_shader_new_instance(struct draw_geometry_shader *gs)
{
   (void)gs;
}

/*
 * Stub function: called from draw_init().
 * Always succeeds.
 */
static inline bool
draw_gs_init(struct draw_context *draw)
{
   (void)draw;
   return true;
}

/*
 * Stub function: called from draw_destroy().
 */
static inline void
draw_gs_destroy(struct draw_context *draw)
{
   (void)draw;
}

/*
 * Stub function: called from draw_pt_fetch_shade_pipeline.c.
 * Should never actually be reached in mesa-strip (no GS bound),
 * but provided to satisfy the linker / compiler.
 */
static inline void
draw_geometry_shader_run(struct draw_geometry_shader *shader,
                         const struct draw_buffer_info *constants,
                         const struct draw_vertex_info *input_verts,
                         const struct draw_prim_info *input_prim,
                         const struct tgsi_shader_info *input_info,
                         uint32_t *const *patch_lengths,
                         struct draw_vertex_info *output_verts,
                         struct draw_prim_info *output_prims)
{
   (void)shader; (void)constants; (void)input_verts;
   (void)input_prim; (void)input_info; (void)patch_lengths;
   (void)output_verts; (void)output_prims;
}

/*
 * Stub: called from draw_pt_fetch_shade_pipeline.c (fetch_pipeline_prepare).
 */
static inline void
draw_geometry_shader_prepare(struct draw_geometry_shader *shader,
                             struct draw_context *draw)
{
   (void)shader; (void)draw;
}

/*
 * Stub for draw_gs_max_output_vertices.
 */
static inline int
draw_gs_max_output_vertices(struct draw_geometry_shader *shader,
                            unsigned pipe_prim)
{
   (void)shader; (void)pipe_prim;
   return 0;
}

#endif /* DRAW_GS_H */
