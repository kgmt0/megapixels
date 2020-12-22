project('megapixels', 'c')
gnome = import('gnome')
gtkdep = dependency('gtk+-3.0')
tiff = dependency('libtiff-4')
threads = dependency('threads')

cc = meson.get_compiler('c')
libm = cc.find_library('m', required: false)

resources = gnome.compile_resources('megapixels-resources', 'org.postmarketos.Megapixels.gresource.xml')

conf = configuration_data()
conf.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set_quoted('SYSCONFDIR', get_option('sysconfdir'))
configure_file(
  output: 'config.h',
  configuration: conf )

# Define DEBUG for debug builds only (debugoptimized is not included on this one)
if get_option('buildtype') == 'debug'
  add_global_arguments('-DDEBUG', language: 'c')
endif

# Workaround for libtiff having ABI changes but not changing the internal version number
if get_option('tiffcfapattern')
  add_global_arguments('-DLIBTIFF_CFA_PATTERN', language: 'c')
endif

executable('megapixels', 'main.c', 'ini.c', 'quickpreview.c', 'camera.c', 'device.c', 'pipeline.c', 'camera_config.c', 'io_pipeline.c', 'process_pipeline.c', 'matrix.c', resources, dependencies : [gtkdep, libm, tiff, threads], install : true)

install_data(['data/org.postmarketos.Megapixels.desktop'],
             install_dir : get_option('datadir') / 'applications')

install_data(['data/org.postmarketos.Megapixels.metainfo.xml'],
             install_dir : get_option('datadir') / 'metainfo')

install_data('data/org.postmarketos.Megapixels.svg',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps')
)

install_data([
  'config/pine64,pinephone-1.0.ini',
  'config/pine64,pinephone-1.1.ini',
  'config/pine64,pinephone-1.2.ini',
  'config/pine64,pinetab.ini',
  ],
  install_dir : get_option('datadir') / 'megapixels/config/')

install_data(['postprocess.sh'],
  install_dir : get_option('datadir') / 'megapixels/',
  install_mode: 'rwxr-xr-x')

# Tools
executable('megapixels-list-devices', 'tools/list_devices.c', 'device.c', dependencies: [gtkdep], install: true)
executable('megapixels-camera-test', 'tools/camera_test.c', 'camera.c', 'device.c', dependencies: [gtkdep], install: true)

test_quickpreview = executable('test_quickpreview', 'tests/test_quickpreview.c', 'quickpreview.c', 'camera.c', dependencies: [gtkdep])
test('quickpreview', test_quickpreview)
