rawmstat_bar_checksum = executable(
  'rawmstat-bar-checksum',
  'bar-checksum.c',
  dependencies: x11_dep,
  build_by_default: false,
)

rawmstat_config_test = find_program('config.sh')
rawmstat_protocol_test = find_program('protocol.sh')
rawmstat_lifecycle_test = find_program('lifecycle.sh')
rawmstat_integration_test = find_program('integration.sh')

rawmstat_default_config = join_paths(
  meson.project_source_root(),
  'config',
  'rawmstat.conf',
)

test(
  'rawmstat-config',
  rawmstat_config_test,
  args: [rawmstat, rawmstat_default_config],
  workdir: meson.project_source_root(),
)

test(
  'rawmstat-lifecycle',
  rawmstat_lifecycle_test,
  args: [rawmstat],
  workdir: meson.project_source_root(),
)

test(
  'rawmstat-protocol',
  rawmstat_protocol_test,
  args: [rawmstat],
  workdir: meson.project_source_root(),
)

test(
  'rawm-rawmstat-integration',
  rawmstat_integration_test,
  args: [rawmstat, rawmstat_bar_checksum, rawm],
  workdir: meson.project_source_root(),
)
