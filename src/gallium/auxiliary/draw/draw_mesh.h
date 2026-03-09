/* Stub - mesh shaders not supported in mesa-strip.
 * Provides struct definition so that code dereferencing
 * ms->info, ms->viewport_index_output, ms->ccdistance_output[], etc. compiles.
 */
#ifndef DRAW_MESH_H
#define DRAW_MESH_H

#include "pipe/p_defines.h"
#include "tgsi/tgsi_scan.h"

struct draw_context;

/**
 * Minimal struct draw_mesh_shader.
 */
struct draw_mesh_shader {
   struct draw_context *draw;

   struct tgsi_shader_info info;

   unsigned position_output;
   unsigned viewport_index_output;
   unsigned clipvertex_output;
   unsigned ccdistance_output[PIPE_MAX_CLIP_OR_CULL_DISTANCE_ELEMENT_COUNT];
   unsigned output_primitive;
};

#endif /* DRAW_MESH_H */
