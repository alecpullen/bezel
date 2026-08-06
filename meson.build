project('bezel', 'c', 'cpp',
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
librsvg   = dependency('librsvg-2.0', required : false)

if librsvg.found()
  add_project_arguments('-DHAVE_LIBRSVG', language : 'cpp')
endif

# NanoVG is vendored under vendor/nanovg.  The GLES2 backend implementation is
# materialised by src/nanovg_shim.cpp; here we only need to compile the core C
# implementation and expose its include path.
nanovg_inc = include_directories('vendor/nanovg/src')
nanovg_lib = static_library('nanovg',
                            'vendor/nanovg/src/nanovg.c',
                            include_directories : nanovg_inc,
                            dependencies : [dependency('threads'),
                                            meson.get_compiler('c').find_library('m', required : false)],
                            install : false)
nanovg_dep = declare_dependency(include_directories : nanovg_inc,
                                link_with : nanovg_lib)

wl_protos_dir = wl_protos.get_variable(pkgconfig : 'pkgdatadir')

# [xml path, output basename]
protocols = [
  [wl_protos_dir / 'stable/xdg-shell/xdg-shell.xml', 'xdg-shell'],
  [meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
   'wlr-layer-shell-unstable-v1'],
  [meson.project_source_root() / 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml',
   'wlr-foreign-toplevel-management-unstable-v1'],
  [meson.project_source_root() / 'protocols/dwl-ipc-unstable-v2.xml',
   'dwl-ipc-unstable-v2'],
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

executable('bezel',
           ['src/main.cpp', 'src/egl.cpp', 'src/panel.cpp',
            'src/context_menu_surface.cpp',
            'src/renderer.cpp', 'src/font_cache.cpp', 'src/clock.cpp',
            'src/layout.cpp', 'src/label.cpp', 'src/battery_service.cpp',
            'src/toplevel_service.cpp', 'src/workspace_service.cpp',
            'src/workspace_switcher.cpp', 'src/window_list.cpp',
            'src/icon_loader.cpp', 'src/battery_widget.cpp',
            'src/desktop_index.cpp', 'src/control_socket.cpp',
            'src/search_engine.cpp',
            'src/nanovg_shim.cpp', gen],
           dependencies : [wl_client, wl_egl, egl, glesv2,
                            freetype, harfbuzz, nanovg_dep, sdbus_cpp, librsvg],
           install : true)