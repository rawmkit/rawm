cc = meson.get_compiler('c')

config_reference_test = executable(
  'config-reference-test',
  'config-reference-test.c',
  rawm_config_source,
  dependencies: libconfig_dep,
  build_by_default: false,
)

dialog_center_client = executable(
  'dialog-center-client',
  'dialog-center-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

fullscreen_rule_client = executable(
  'fullscreen-rule-client',
  'fullscreen-rule-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

attention_rule_client = executable(
  'attention-rule-client',
  'attention-rule-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

geometry_client = executable(
  'geometry-client',
  'geometry-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

tagbar_client = executable(
  'tagbar-client',
  'tagbar-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

dl_dep = cc.find_library('dl', required: false)
send_key_deps = [x11_dep]
if dl_dep.found()
  send_key_deps += dl_dep
endif

send_key = executable(
  'send-key',
  'send-key.c',
  dependencies: send_key_deps,
  build_by_default: false,
)

status_client = executable(
  'status-client',
  'status-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

config_test = find_program('config.sh')
gaps_test = find_program('gaps.sh')
dialog_center_test = find_program('dialog-center.sh')
fullscreen_rule_test = find_program('fullscreen-rule.sh')
attention_rule_test = find_program('attention-rule.sh')
geometry_test = find_program('geometry.sh')
placement_test = find_program('placement.sh')
tagbar_test = find_program('tagbar.sh')
status_test = find_program('status.sh')
signals_test = find_program('signals.sh')

test_workdir = meson.project_source_root()

test(
  'config',
  config_test,
  args: [rawm],
  workdir: test_workdir,
)

test(
  'config-reference',
  config_reference_test,
)

test(
  'gaps',
  gaps_test,
  args: [rawm],
  workdir: test_workdir,
)

test(
  'dialog-center',
  dialog_center_test,
  args: [rawm, dialog_center_client],
  workdir: test_workdir,
)

test(
  'fullscreen-rule',
  fullscreen_rule_test,
  args: [rawm, fullscreen_rule_client],
  workdir: test_workdir,
)

test(
  'attention-rule',
  attention_rule_test,
  args: [rawm, attention_rule_client],
  workdir: test_workdir,
)

test(
  'geometry',
  geometry_test,
  args: [rawm, geometry_client, send_key],
  workdir: test_workdir,
)

test(
  'placement',
  placement_test,
  args: [rawm, geometry_client],
  workdir: test_workdir,
)

test(
  'tagbar',
  tagbar_test,
  args: [rawm, tagbar_client],
  workdir: test_workdir,
)

test(
  'status',
  status_test,
  args: [rawm, status_client],
  workdir: test_workdir,
)

test(
  'signals',
  signals_test,
  args: [rawm],
  workdir: test_workdir,
)
