# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

subdir('gtest')
subdir('c11/impl')
subdir('android_stub')
subdir('util')

inc_mesa = include_directories('mesa')
inc_src = include_directories('.')
inc_gallium = include_directories('gallium/include')
inc_gallium_aux = include_directories('gallium/auxiliary')
inc_amd_common = include_directories('.')  # stub - not used
inc_tool = include_directories('tool')
inc_frontends = include_directories('gallium/frontends')
pps_datasources = []
pps_includes = []

inc_llvmpipe = include_directories('gallium/drivers/softpipe')  # stub

if dep_llvm.found()
  inc_amd_common_llvm = []
else
  inc_amd_common_llvm = []
endif

glsl_util_files = files(
    'mesa/main/extensions_table.c',
    'mesa/program/prog_parameter.c',
    'mesa/program/symbol_table.c',
    'mesa/program/dummy_errors.c',
)

sha1_h = custom_target(
  'git_sha1.h',
  output : 'git_sha1.h',
  command : [prog_python, git_sha1_gen_py, '--output', '@OUTPUT@'],
  build_by_default : true,
  build_always_stale : true,
)

if cc.get_argument_syntax() == 'msvc'
  subdir('getopt')
else
  idep_getopt = null_dep
endif
if with_platform_x11
  subdir('x11')
endif
if with_gallium_or_lvp or with_gbm or with_platform_wayland
  subdir('loader')
endif
subdir('compiler')

if with_wayland_bind_display
  subdir('egl/wayland/wayland-drm')
endif

if with_gallium
  subdir('mesa')
endif
if with_gallium_or_lvp
  subdir('gallium')
endif
# These require libgallium (shared_glapi_lib)
if with_gallium and (with_glx != 'disabled' or with_egl)
  if with_gles1 and not with_glvnd
    subdir('mesa/glapi/es1api')
  endif
  if with_gles2 and not with_glvnd
    subdir('mesa/glapi/es2api')
  endif
endif
if with_glx == 'dri'
  subdir('glx')
endif

if with_gbm
  if get_option('libgbm-external')
    dep_gbm = dependency('gbm')
  else
    subdir('gbm')
  endif
else
  dep_gbm = null_dep
endif

if with_egl
  subdir('egl')
endif
if with_gallium
  if with_glx == 'dri' or with_platform_x11 or with_platform_xcb
    subdir('gallium/targets/dril')
  endif
endif

if with_gbm and with_dri2
  subdir('gbm/backends/dri')
endif

if with_glx != 'disabled' and not with_glvnd
  pkg.generate(
    name : 'gl',
    description : 'Mesa OpenGL Library',
    version : meson.project_version(),
    libraries : libgl,
    libraries_private : gl_priv_libs,
    requires_private : gl_priv_reqs,
    variables : ['glx_tls=yes'],
  )
endif

subdir('tool')
