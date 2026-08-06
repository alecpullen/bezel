project('nanovg', 'c',
        version : '0.1.0')

# The main project materialises the GLES2 backend implementation via
# src/nanovg_shim.cpp; we just compile the core NanoVG implementation here
# and expose its include path.
nanovg_inc = include_directories('src')
nanovg_lib = static_library('nanovg',
                            'src/nanovg.c',
                            include_directories : nanovg_inc,
                            dependencies : [dependency('threads'),
                                            meson.get_compiler('c').find_library('m', required : false)],
                            install : false)

nanovg_dep = declare_dependency(include_directories : nanovg_inc,
                                link_with : nanovg_lib)
