# Copyright © 2017 Dylan Baker
# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

sym_config = configuration_data()

foreach d : [[false, 'radeon_drm_winsys_create'],
             [false, 'amdgpu_winsys_create'],
             [false, 'nouveau_drm_screen_create'],
             [false, 'fd_drm_screen_create_renderonly'],
             [false, 'ac_init_shared_llvm_once']]
  sym_config.set(d[1], '')
endforeach

if with_gallium_va
  sym_config.set('va_driver_init', '__vaDriverInit_*_*;')
else
  sym_config.set('va_driver_init', '')
endif

inc_gallium_drivers = include_directories('drivers')
inc_gallium_winsys = include_directories('winsys')
inc_gallium_winsys_sw = include_directories('winsys/sw')

subdir('auxiliary')
subdir('auxiliary/pipe-loader')
subdir('winsys/sw/null')
if with_dri
  subdir('winsys/sw/dri')
else
  libswdri = []
endif
if with_gallium_drisw_kms
  subdir('winsys/sw/kms-dri')
else
  libswkmsdri = []
endif
if with_platform_windows
  subdir('winsys/sw/gdi')
else
  libwsgdi = null_dep
endif
subdir('winsys/sw/wrapper')

# softpipe only
if with_gallium_mypipe
  subdir('drivers/mypipe')
else
  driver_softpipe = declare_dependency()
endif
if with_gallium_softpipe
  subdir('drivers/softpipe')
else
  driver_softpipe = declare_dependency()
endif

# no llvmpipe
driver_llvmpipe = declare_dependency()

if with_gallium_mypipe
  driver_swrast = driver_mypipe
elif with_gallium_softpipe
  driver_swrast = driver_softpipe
else
  driver_swrast = declare_dependency()
endif

# all hardware drivers disabled
driver_asahi = declare_dependency()
driver_r300 = declare_dependency()
driver_r600 = declare_dependency()
driver_radeonsi = declare_dependency()
driver_nouveau = declare_dependency()
driver_freedreno = declare_dependency()
driver_vc4 = declare_dependency()
driver_panfrost = declare_dependency()
driver_etnaviv = declare_dependency()
driver_kmsro = declare_dependency()
driver_v3d = declare_dependency()
driver_tegra = declare_dependency()
driver_crocus = declare_dependency()
driver_iris = declare_dependency()
driver_i915 = declare_dependency()
driver_svga = declare_dependency()
driver_virgl = declare_dependency()
driver_lima = declare_dependency()
driver_rocket = declare_dependency()
driver_ethosu = declare_dependency()
driver_zink = declare_dependency()
driver_d3d12 = declare_dependency()

if with_glx == 'xlib'
  subdir('winsys/sw/xlib')
  subdir('frontends/glx/xlib')
  subdir('targets/libgl-xlib')
endif
if with_dri
  subdir('frontends/dri')
  subdir('targets/dri')
endif
