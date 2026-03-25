files_mypipe = files(
    'mp_screen.c',
    'mp_screen.h',
    'mp_context.c',
    'mp_context.h',
    'mp_fence.c',
    'mp_fence.h',
    'mp_texture.c',
    'mp_texture.h',
    'mp_public.h',
    'mp_state.h',
    'mp_state_blend.c',
    'mp_state_clip.c',
    'mp_state_rasterizer.c',
    'mp_state_sampler.c',
    'mp_state_shader.c',
    'mp_state_streamout.c',
    'mp_state_texture.c',
    'mp_state_vertex.c',
    'mp_state_image.c',
    'mp_state_query.c',
    'mp_flush.c',
    'mp_flush.h',
    'mp_draw.c',
    'mp_shader_exec.c',
    'mp_raster.c',
)

libmypipe = static_library(
    'mypipe',
    files_mypipe,
    include_directories : [inc_gallium_aux, inc_gallium, inc_include, inc_src],
    c_args : [c_msvc_compat_args],
    gnu_symbol_visibility : 'hidden',
    dependencies : [idep_nir, idep_mesautil],
)

driver_mypipe = declare_dependency(
    compile_args : '-DGALLIUM_MYPIPE',
    link_with : libmypipe,
)
