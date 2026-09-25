rawm_status_lib = static_library(
  'rawm-status',
  'status.c',
  install: false,
)

rawm_status_dep = declare_dependency(
  link_with: rawm_status_lib,
  include_directories: include_directories('.'),
)
