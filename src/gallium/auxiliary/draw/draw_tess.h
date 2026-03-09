/* Stub - tessellation not supported in mesa-strip.
 * Provides struct definitions and no-op inline stubs so that code
 * referencing TCS/TES types/functions still compiles.
 */
#ifndef DRAW_TESS_H
#define DRAW_TESS_H

#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "tgsi/tgsi_scan.h"
#include "draw_context.h"

struct draw_context;
struct draw_vertex_info;
struct draw_prim_info;

/**
 * Minimal struct draw_tess_ctrl_shader -- just enough members so that
 * code dereferencing tcs->info, tcs->state etc. compiles.
 */
struct draw_tess_ctrl_shader {
   struct draw_context *draw;

   struct pipe_shader_state state;
   struct tgsi_shader_info info;

   unsigned vector_length;
   unsigned vertices_out;
};

/**
 * Minimal struct draw_tess_eval_shader -- just enough members so that
 * code dereferencing tes->info, tes->state, tes->viewport_index_output,
 * tes->ccdistance_output[], tes->prim_mode etc. compiles.
 */
struct draw_tess_eval_shader {
   struct draw_context *draw;

   struct pipe_shader_state state;
   struct tgsi_shader_info info;

   enum mesa_prim prim_mode;
   unsigned spacing;
   unsigned vertex_order_cw;
   unsigned point_mode;

   unsigned position_output;
   unsigned viewport_index_output;
   unsigned clipvertex_output;
   unsigned ccdistance_output[PIPE_MAX_CLIP_OR_CULL_DISTANCE_ELEMENT_COUNT];
};

/*
 * Stub: called from draw_pt.c (draw_pt_arrays).
 * Returns MESA_PRIM_TRIANGLES as a safe default; should never actually
 * be reached in mesa-strip since no TES will be bound.
 */
static inline enum mesa_prim
get_tes_output_prim(struct draw_tess_eval_shader *shader)
{
   if (shader)
      return shader->prim_mode;
   return MESA_PRIM_TRIANGLES;
}

#endif /* DRAW_TESS_H */
