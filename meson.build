project('megapixels', 'c', version: '1.3.2')

gnome = import('gnome')
gtkdep = dependency('gtk4')
tiff = dependency('libtiff-4')
zbar = dependency('zbar')
threads = dependency('threads')
# gl = dependency('gl')
epoxy      = dependency('epoxy')

cc = meson.get_compiler('c')
libm = cc.find_library('m', required: false)

subdir('data')

conf = configuration_data()
conf.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set_quoted('SYSCONFDIR', get_option('sysconfdir'))
configure_file(
  output: 'config.h',
  configuration: conf)

add_global_arguments('-DVERSION="@0@"'.format(meson.project_version()), language: 'c')

# Define DEBUG for debug builds only (debugoptimized is not included on this one)
if get_option('buildtype') == 'debug'
  add_global_arguments('-DDEBUG', language: 'c')
endif

# Workaround for libtiff having ABI changes but not changing the internal
# version number
if get_option('tiffcfapattern')
  add_global_arguments('-DLIBTIFF_CFA_PATTERN', language: 'c')
endif

executable('megapixels',
  'src/camera.c',
  'src/camera_config.c',
  'src/device.c',
  'src/flash.c',
  'src/gl_util.c',
  'src/gles2_debayer.c',
  'src/ini.c',
  'src/io_pipeline.c',
  'src/main.c',
  'src/matrix.c',
  'src/pipeline.c',
  'src/process_pipeline.c',
  'src/zbar_pipeline.c',
  resources,
  include_directories: 'src/',
  dependencies: [gtkdep, libm, tiff, zbar, threads, epoxy],
  install: true,
  link_args: '-Wl,-ldl')

install_data(
  [
    'config/pine64,pinephone-1.0.ini',
    'config/pine64,pinephone-1.1.ini',
    'config/pine64,pinephone-1.2.ini',
    'config/pine64,pinetab.ini',
  ],
  install_dir: get_option('datadir') / 'megapixels/config/')

# Tools
executable('megapixels-list-devices',
  'tools/list_devices.c',
  'src/device.c',
  include_directories: 'src/',
  dependencies: [gtkdep],
  install: true)

executable('megapixels-camera-test',
  'tools/camera_test.c',
  'src/camera.c',
  'src/device.c',
  include_directories: 'src/',
  dependencies: [gtkdep],
  install: true)
