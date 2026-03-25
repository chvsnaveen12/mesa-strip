#ifndef MP_SHADER_EXEC_H
#define MP_SHADER_EXEC_H

#include <stdint.h>
#include "mp_raster.h"

struct mp_compiled_shader;
struct mypipe_context;

/* Register type for the interpreter */
typedef union { float f; uint32_t u; int32_t i; } mp_reg;
#define MP_MAX_HW_REGS 32
#define MP_MAX_SSA     128

struct mp_vs_output {
    float position[4];
    float varyings[MP_MAX_VARYINGS][4];
    unsigned num_varyings;
    float point_size;                       /* gl_PointSize, 0 = use fixed-function */
};

/* Run vertex shader on one vertex.
 * inputs:  array of fetched attributes [attrib][xyzw]
 * out:     filled with position + varyings */
void mp_run_vs(const struct mp_compiled_shader *shader,
               const struct mypipe_context *ctx,
               const float inputs[][4], unsigned num_inputs,
               const void *uniforms, struct mp_vs_output *out);

/* Run fragment shader on a 2x2 quad.
 * Reads quad->varyings, writes quad->color_out for all 4 pixels. */
void mp_run_fs(const struct mp_compiled_shader *shader,
               const struct mypipe_context *ctx,
               const void *uniforms, struct mp_quad *quad);

#endif
