rawmstat_signal_number = executable(
  'rawmstat-signal-number',
  'signal-number.c',
  build_by_default: false,
)

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
  args: [rawmstat, rawmstat_signal_number],
  workdir: meson.project_source_root(),
)

# The shell fixtures retain their own capability guards for direct invocation.
# The combined repository always passes the in-tree rawm executable to the
# integration test, so producer/consumer compatibility is no longer optional.
test(
  'rawmstat-protocol',
  rawmstat_protocol_test,
  args: [rawmstat, rawmstat_signal_number],
  workdir: meson.project_source_root(),
)

test(
  'rawm-rawmstat-integration',
  rawmstat_integration_test,
  args: [rawmstat, rawmstat_signal_number, rawmstat_bar_checksum, rawm],
  workdir: meson.project_source_root(),
)
