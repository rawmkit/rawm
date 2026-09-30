rawm_src_include = include_directories('.')
rawm_config_source = files('rawm-config.c')
rawm_color_source = files('rawm-color.c')

rawm = executable(
  'rawm',
  'rawm.c',
  'rawm-control.c',
  rawm_color_source,
  rawm_config_source,
  c_args: rawm_c_args,
  dependencies: rawm_deps,
  install: true,
)
