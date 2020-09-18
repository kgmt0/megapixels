project('megapixels', 'c')
gnome = import('gnome')
gtkdep = dependency('gtk+-3.0')

cc = meson.get_compiler('c')
libm = cc.find_library('m', required: false)

resources = gnome.compile_resources('megapixels-resources', 'org.postmarketos.Megapixels.gresource.xml')

executable('megapixels', 'main.c', 'ini.c', 'bayer.c', 'quickdebayer.c', resources, dependencies : [gtkdep, libm], install : true)

install_data(['org.postmarketos.Megapixels.desktop'],
             install_dir : get_option('datadir') / 'applications')

install_data('org.postmarketos.Megapixels.svg',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps')
)
