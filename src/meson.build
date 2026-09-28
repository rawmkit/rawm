rawm_src_include = include_directories('.')
rawm_config_source = files('rawm-config.c', 'status/config.c')

rawm = executable(
  'rawm',
  'rawm.c',
  'status/alsa.c',
  'status/source.c',
  'status/worker.c',
  rawm_config_source,
  c_args: rawm_c_args,
  dependencies: rawm_deps + [rawm_status_dep],
  install: true,
)


subdir('rawmctl')

subdir('rawmsession')
