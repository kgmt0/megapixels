resources = gnome.compile_resources('megapixels-resources',
  'org.postmarketos.Megapixels.gresource.xml')

install_data(['org.postmarketos.Megapixels.desktop'],
  install_dir: get_option('datadir') / 'applications')

install_data(['org.postmarketos.Megapixels.metainfo.xml'],
  install_dir: get_option('datadir') / 'metainfo')

install_data('org.postmarketos.Megapixels.svg',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps'))

install_data(['postprocess.sh'],
  install_dir: get_option('datadir') / 'megapixels/',
  install_mode: 'rwxr-xr-x')
