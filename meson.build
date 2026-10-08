project('xfce4-journal-plugin', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: ['c_std=gnu11', 'warning_level=2'])

gtk_dep     = dependency('gtk+-3.0')
panel_dep   = dependency('libxfce4panel-2.0')
glib_dep    = dependency('glib-2.0')
jansson_dep = dependency('jansson')

add_project_arguments('-DPACKAGE_VERSION="' + meson.project_version() + '"', language: 'c')

core_sources = files(
  'src/config.c',
  'src/template.c',
  'src/layout.c',
  'src/markdown_editor.c',
  'src/entry_writer.c',
)

plugin_sources = core_sources + files(
  'src/plugin.c',
  'src/panel_widget.c',
  'src/preferences.c',
)

plugins_dir = get_option('prefix') / get_option('libdir') / 'xfce4' / 'panel' / 'plugins'
desktop_dir = get_option('prefix') / get_option('datadir') / 'xfce4' / 'panel' / 'plugins'
icons_dir   = get_option('prefix') / get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'

shared_module('xfce4-journal-plugin',
  plugin_sources,
  dependencies: [gtk_dep, panel_dep, jansson_dep],
  install: true,
  install_dir: plugins_dir,
)

desktop_conf = configure_file(
  input: 'data/xfce4-journal-plugin.desktop.in',
  output: 'xfce4-journal-plugin.desktop',
  copy: true,
)
install_data(desktop_conf, install_dir: desktop_dir)

install_data('data/icons/xfce4-journal-plugin.svg', install_dir: icons_dir)
install_data('data/icons/xfce4-journal-plugin-symbolic.svg', install_dir: icons_dir)

test_exe = executable('test_journal',
  'tests/test_journal.c',
  core_sources,
  dependencies: [glib_dep, jansson_dep],
)
test('journal', test_exe)
