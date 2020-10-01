project('megapixels', 'c')
gnome = import('gnome')
gtkdep = dependency('gtk+-3.0')
tiff = dependency('libtiff-4')

cc = meson.get_compiler('c')
libm = cc.find_library('m', required: false)

resources = gnome.compile_resources('megapixels-resources', 'org.postmarketos.Megapixels.gresource.xml')

conf = configuration_data()
conf.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set_quoted('SYSCONFDIR', get_option('sysconfdir'))
configure_file(
  output: 'config.h',
  configuration: conf )

executable('megapixels', 'main.c', 'ini.c', 'quickdebayer.c', resources, dependencies : [gtkdep, libm, tiff], install : true)

install_data(['org.postmarketos.Megapixels.desktop'],
             install_dir : get_option('datadir') / 'applications')

install_data('org.postmarketos.Megapixels.svg',
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
