/*
 * Stub implementations for geometry shader, tessellation shader, and
 * mesh shader create/bind/delete functions.
 *
 * The real implementations lived in draw_gs.c, draw_tess.c, and
 * draw_mesh.c which have been removed from the mesa-strip build.
 * These stubs allow the code to compile and link; at runtime
 * the create functions return NULL, and bind/delete are no-ops.
 */

#include "draw_context.h"
#include "draw_gs.h"
#include "draw_tess.h"
#include "draw_mesh.h"
#include "draw_private.h"


/* --- Geometry shader stubs --- */

struct draw_geometry_shader *
draw_create_geometry_shader(struct draw_context *draw,
                            const struct pipe_shader_state *shader)
{
   (void)draw; (void)shader;
   return NULL;
}

void
draw_bind_geometry_shader(struct draw_context *draw,
                          struct draw_geometry_shader *dvs)
{
   draw->gs.geometry_shader = dvs;
   draw->gs.num_gs_outputs = dvs ? dvs->info.num_outputs : 0;
   draw->gs.position_output = dvs ? dvs->position_output : 0;
   draw->gs.clipvertex_output = dvs ? dvs->clipvertex_output : 0;
}

void
draw_delete_geometry_shader(struct draw_context *draw,
                            struct draw_geometry_shader *dvs)
{
   (void)draw; (void)dvs;
   /* Nothing to free -- create always returned NULL. */
}


/* --- Tessellation control shader stubs --- */

struct draw_tess_ctrl_shader *
draw_create_tess_ctrl_shader(struct draw_context *draw,
                             const struct pipe_shader_state *shader)
{
   (void)draw; (void)shader;
   return NULL;
}

void
draw_bind_tess_ctrl_shader(struct draw_context *draw,
                           struct draw_tess_ctrl_shader *dvs)
{
   draw->tcs.tess_ctrl_shader = dvs;
}

void
draw_delete_tess_ctrl_shader(struct draw_context *draw,
                             struct draw_tess_ctrl_shader *dvs)
{
   (void)draw; (void)dvs;
}


/* --- Tessellation evaluation shader stubs --- */

struct draw_tess_eval_shader *
draw_create_tess_eval_shader(struct draw_context *draw,
                             const struct pipe_shader_state *shader)
{
   (void)draw; (void)shader;
   return NULL;
}

void
draw_bind_tess_eval_shader(struct draw_context *draw,
                           struct draw_tess_eval_shader *dvs)
{
   draw->tes.tess_eval_shader = dvs;
   draw->tes.num_tes_outputs = dvs ? dvs->info.num_outputs : 0;
   draw->tes.position_output = dvs ? dvs->position_output : 0;
   draw->tes.clipvertex_output = dvs ? dvs->clipvertex_output : 0;
}

void
draw_delete_tess_eval_shader(struct draw_context *draw,
                             struct draw_tess_eval_shader *dvs)
{
   (void)draw; (void)dvs;
}


/* --- Mesh shader stubs --- */

struct draw_mesh_shader *
draw_create_mesh_shader(struct draw_context *draw,
                        const struct pipe_shader_state *shader)
{
   (void)draw; (void)shader;
   return NULL;
}

void
draw_bind_mesh_shader(struct draw_context *draw,
                      struct draw_mesh_shader *dvs)
{
   draw->ms.mesh_shader = dvs;
   draw->ms.num_ms_outputs = dvs ? dvs->info.num_outputs : 0;
   draw->ms.position_output = dvs ? dvs->position_output : 0;
   draw->ms.clipvertex_output = dvs ? dvs->clipvertex_output : 0;
}

void
draw_delete_mesh_shader(struct draw_context *draw,
                        struct draw_mesh_shader *dvs)
{
   (void)draw; (void)dvs;
}
