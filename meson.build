project('camera', 'c')
gtkdep = dependency('gtk+-3.0')
executable('camera', 'main.c', 'ini.c', dependencies : gtkdep, install : true)

install_data(['camera.glade', 'camera.css'],
             install_dir : get_option('datadir') / 'camera/ui')