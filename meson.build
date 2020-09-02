project('megapixels', 'c')
gtkdep = dependency('gtk+-3.0')
executable('megapixels', 'main.c', 'ini.c', 'bayer.c', dependencies : gtkdep, install : true)

install_data(['camera.glade', 'camera.css'],
             install_dir : get_option('datadir') / 'camera/ui')
