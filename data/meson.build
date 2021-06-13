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

settings_schemas = ['org.postmarketos.Megapixels.gschema.xml']
schemas_dir = get_option('prefix') / get_option('datadir') / 'glib-2.0' / 'schemas'
install_data(settings_schemas, install_dir: schemas_dir)
gnome.compile_schemas(depend_files: files(settings_schemas))
meson.add_install_script('glib-compile-schemas', schemas_dir)
