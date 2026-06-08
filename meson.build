project('myshell', 'c', 'cpp',
        version : '0.1.0',
        default_options : ['cpp_std=c++20', 'warning_level=2'])

wl_client = dependency('wayland-client')
wl_egl    = dependency('wayland-egl')
egl       = dependency('egl')
glesv2    = dependency('glesv2')
wl_protos = dependency('wayland-protocols')
scanner   = find_program('wayland-scanner')

sdbus_cpp = dependency('sdbus-c++')
freetype  = dependency('freetype2', required : false)
harfbuzz  = dependency('harfbuzz', required : false)
nanovg    = subproject('nanovg').get_variable('nanovg_dep')

wl_protos_dir = wl_protos.get_variable(pkgconfig : 'pkgdatadir')

# [xml path, output basename]
protocols = [
  [wl_protos_dir / 'stable/xdg-shell/xdg-shell.xml', 'xdg-shell'],
  [meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
   'wlr-layer-shell-unstable-v1'],
]

gen = []
foreach p : protocols
  gen += custom_target(p[1] + '-client-header',
                       input : p[0], output : p[1] + '-client-protocol.h',
                       command : [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  gen += custom_target(p[1] + '-private-code',
                       input : p[0], output : p[1] + '-protocol.c',
                       command : [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

executable('myshell',
           ['src/main.cpp', 'src/egl.cpp', 'src/panel.cpp',
            'src/renderer.cpp', 'src/font_cache.cpp', 'src/clock.cpp',
            'src/layout.cpp', 'src/label.cpp', 'src/battery_service.cpp',
            'src/nanovg_shim.cpp', gen],
           dependencies : [wl_client, wl_egl, egl, glesv2,
                            freetype, harfbuzz, nanovg, sdbus_cpp],
           install : true)