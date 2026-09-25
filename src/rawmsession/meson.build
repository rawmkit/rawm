rawmsession_config_source = files('rawmsession-config.c')
rawmsession_include = include_directories('.')

rawmsession = executable(
  'rawmsession',
  'rawmsession.c',
  rawmsession_config_source,
  include_directories: rawmsession_include,
  c_args: [
    '-DVERSION="@0@"'.format(meson.project_version()),
    '-DSYSCONFDIR="@0@"'.format(runtime_sysconfdir),
  ],
  dependencies: [libconfig_dep],
  install: true,
)
