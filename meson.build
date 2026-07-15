project('sdxi', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3'])

cc = meson.get_compiler('c')

add_project_arguments('-Wno-unused-parameter', language : 'c')

libsdxi_root = 'libsdxi'
libsdxi_inc = include_directories(join_paths(libsdxi_root, 'include'))
cli_inc = include_directories('src')
libsdxi_dep = cc.find_library('sdxi',
  dirs : [join_paths(meson.project_source_root(), libsdxi_root, 'src', '.libs')],
  static : true,
  required : true)

executable('sdxi',
  'src/sdxi.c',
  'src/attrs.c',
  'src/cli_cmd.c',
  include_directories : [libsdxi_inc, cli_inc],
  dependencies : [libsdxi_dep],
  install : true)
