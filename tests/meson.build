cc = meson.get_compiler('c')

config_reference_test = executable(
  'config-reference-test',
  'config-reference-test.c',
  rawm_config_source,
  include_directories: rawm_src_include,
  dependencies: [libconfig_dep, rawm_status_dep],
  build_by_default: false,
)

config_defaults_test = executable(
  'config-defaults-test',
  'config-defaults-test.c',
  rawm_config_source,
  include_directories: rawm_src_include,
  dependencies: [libconfig_dep, rawm_status_dep],
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

compositor_state_client = executable(
  'compositor-state-client',
  'compositor-state-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

rawmctl_client = executable(
  'rawmctl-client',
  'rawmctl-client.c',
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

send_pointer = executable(
  'send-pointer',
  'send-pointer.c',
  dependencies: send_key_deps,
  build_by_default: false,
)

status_client = executable(
  'status-client',
  'status-client.c',
  dependencies: x11_dep,
  build_by_default: false,
)

if xinerama_dep.found()
  xinerama_probe = executable(
    'xinerama-probe',
    'xinerama-probe.c',
    dependencies: [x11_dep, xinerama_dep],
    build_by_default: false,
  )
endif

if get_option('systray')
  systray_client = executable(
    'systray-client',
    'systray-client.c',
    dependencies: x11_dep,
    build_by_default: false,
  )
endif

config_test = find_program('config.sh')
gaps_test = find_program('gaps.sh')
dialog_center_test = find_program('dialog-center.sh')
fullscreen_rule_test = find_program('fullscreen-rule.sh')
attention_rule_test = find_program('attention-rule.sh')
geometry_test = find_program('geometry.sh')
compositor_state_test = find_program('compositor-state.sh')
rawmctl_test = find_program('rawmctl.sh')
focus_index_test = find_program('focus-index.sh')
window_view_test = find_program('window-view.sh')
actions_test = find_program('actions.sh')
if xinerama_dep.found()
  multimon_test = find_program('multimon.sh')
endif
placement_test = find_program('placement.sh')
tagbar_test = find_program('tagbar.sh')
status_test = find_program('status.sh')
if get_option('systray')
  systray_test = find_program('systray.sh')
endif
signals_test = find_program('signals.sh')
skip_test = find_program('support/skip.sh')
xvfb_probe = find_program('support/probe-xvfb.sh')

# Integration-test fixtures are build-machine programs. Discover them during
# Meson setup so missing optional tooling is visible before the suite runs.
# Xvfb availability is a runtime capability rather than a package-name check:
# an installed server can still be unusable when its XKB data or compiler is
# missing or inconsistent. Probe one disposable server here and register the
# X11 integration tests as skips when that fixture cannot actually start.
# The shell scripts keep their own guards for direct/manual invocation.
xvfb_program = find_program('Xvfb', required: false, native: true)
xwininfo_program = find_program('xwininfo', required: false, native: true)
xkbcomp_program = find_program('xkbcomp', required: false, native: true)
xterm_program = find_program('xterm', required: false, native: true)
awk_program = find_program('awk', required: false, native: true)
sed_program = find_program('sed', required: false, native: true)
grep_program = find_program('grep', required: false, native: true)
sort_program = find_program('sort', required: false, native: true)
wc_program = find_program('wc', required: false, native: true)
xkeyboard_config_dep = dependency(
  'xkeyboard-config',
  required: false,
  native: true,
)

xvfb_runtime_ok = false
xvfb_runtime_summary = 'not probed'
if xvfb_program.found() and xwininfo_program.found()
  xvfb_probe_result = run_command(
    xvfb_probe,
    xvfb_program.full_path(),
    xwininfo_program.full_path(),
    check: false,
  )
  if xvfb_probe_result.returncode() == 0
    xvfb_runtime_ok = true
    xvfb_runtime_summary = 'usable'
  else
    xvfb_runtime_summary = 'unavailable'
    xvfb_probe_diagnostic = xvfb_probe_result.stderr().strip()
    if xvfb_probe_diagnostic == ''
      xvfb_probe_diagnostic = xvfb_probe_result.stdout().strip()
    endif
    if xvfb_probe_diagnostic == ''
      xvfb_probe_diagnostic = 'probe exited with status @0@'.format(
        xvfb_probe_result.returncode(),
      )
    endif
    warning('Xvfb test fixture is unusable\n@0@'.format(xvfb_probe_diagnostic))
  endif
endif

summary(
  {
    'Xvfb': xvfb_program.found(),
    'xwininfo': xwininfo_program.found(),
    'Xvfb/XKB runtime': xvfb_runtime_summary,
    'xkbcomp': xkbcomp_program.found(),
    'xkeyboard-config metadata': xkeyboard_config_dep.found(),
    'xterm': xterm_program.found(),
    'awk': awk_program.found(),
    'sed': sed_program.found(),
    'grep': grep_program.found(),
    'sort': sort_program.found(),
    'wc': wc_program.found(),
  },
  section: 'Test fixtures',
  bool_yn: true,
)

x11_missing = []
if not xvfb_program.found()
  x11_missing += ['Xvfb']
endif
if not xwininfo_program.found()
  x11_missing += ['xwininfo']
endif
if xvfb_program.found() and xwininfo_program.found() and not xvfb_runtime_ok
  x11_missing += ['usable Xvfb/XKB runtime']
endif

test_workdir = meson.project_source_root()

config_missing = []
if not grep_program.found()
  config_missing += ['grep']
endif
if config_missing.length() == 0
  test(
    'config',
    config_test,
    args: [rawm],
    workdir: test_workdir,
  )
else
  test(
    'config',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(config_missing))],
  )
endif

test(
  'config-reference',
  config_reference_test,
)

test(
  'config-defaults',
  config_defaults_test,
  args: [join_paths(meson.project_source_root(), 'config', 'rawm.conf')],
)

gaps_missing = x11_missing
if not xterm_program.found()
  gaps_missing += ['xterm']
endif
if not sed_program.found()
  gaps_missing += ['sed']
endif
if not sort_program.found()
  gaps_missing += ['sort']
endif
if gaps_missing.length() == 0
  test(
    'gaps',
    gaps_test,
    args: [rawm],
    workdir: test_workdir,
  )
else
  test(
    'gaps',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(gaps_missing))],
  )
endif

dialog_center_missing = x11_missing
if not awk_program.found()
  dialog_center_missing += ['awk']
endif
if not sed_program.found()
  dialog_center_missing += ['sed']
endif
if dialog_center_missing.length() == 0
  test(
    'dialog-center',
    dialog_center_test,
    args: [rawm, dialog_center_client],
    workdir: test_workdir,
  )
else
  test(
    'dialog-center',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(dialog_center_missing))],
  )
endif

fullscreen_rule_missing = x11_missing
if not awk_program.found()
  fullscreen_rule_missing += ['awk']
endif
if not sed_program.found()
  fullscreen_rule_missing += ['sed']
endif
if not wc_program.found()
  fullscreen_rule_missing += ['wc']
endif
if fullscreen_rule_missing.length() == 0
  test(
    'fullscreen-rule',
    fullscreen_rule_test,
    args: [rawm, fullscreen_rule_client],
    workdir: test_workdir,
  )
else
  test(
    'fullscreen-rule',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(fullscreen_rule_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'attention-rule',
    attention_rule_test,
    args: [rawm, attention_rule_client],
    workdir: test_workdir,
  )
else
  test(
    'attention-rule',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

geometry_missing = x11_missing
if not awk_program.found()
  geometry_missing += ['awk']
endif
if not sed_program.found()
  geometry_missing += ['sed']
endif
if not wc_program.found()
  geometry_missing += ['wc']
endif
if geometry_missing.length() == 0
  test(
    'geometry',
    geometry_test,
    args: [rawm, geometry_client, send_key],
    workdir: test_workdir,
  )
else
  test(
    'geometry',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(geometry_missing))],
  )
endif

compositor_state_missing = x11_missing
if compositor_state_missing.length() == 0
  test(
    'compositor-state',
    compositor_state_test,
    args: [rawm, compositor_state_client, send_key, send_pointer],
    workdir: test_workdir,
  )
else
  test(
    'compositor-state',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(compositor_state_missing))],
  )
endif

rawmctl_missing = x11_missing
if not grep_program.found()
  rawmctl_missing += ['grep']
endif
if not sed_program.found()
  rawmctl_missing += ['sed']
endif
if rawmctl_missing.length() == 0
  test(
    'rawmctl',
    rawmctl_test,
    args: [rawm, rawmctl, rawmctl_client],
    workdir: test_workdir,
  )
else
  test(
    'rawmctl',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(rawmctl_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'focus-index',
    focus_index_test,
    args: [rawm, compositor_state_client, send_key],
    workdir: test_workdir,
  )
else
  test(
    'focus-index',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'window-view',
    window_view_test,
    args: [rawm, compositor_state_client, send_key],
    workdir: test_workdir,
  )
else
  test(
    'window-view',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'actions',
    actions_test,
    args: [rawm, compositor_state_client, send_key],
    workdir: test_workdir,
  )
else
  test(
    'actions',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

placement_missing = x11_missing
if not awk_program.found()
  placement_missing += ['awk']
endif
if not sed_program.found()
  placement_missing += ['sed']
endif
if placement_missing.length() == 0
  test(
    'placement',
    placement_test,
    args: [rawm, geometry_client],
    workdir: test_workdir,
  )
else
  test(
    'placement',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(placement_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'tagbar',
    tagbar_test,
    args: [rawm, tagbar_client],
    workdir: test_workdir,
  )
else
  test(
    'tagbar',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

if x11_missing.length() == 0
  test(
    'status',
    status_test,
    args: [rawm, status_client],
    workdir: test_workdir,
  )
else
  test(
    'status',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
  )
endif

if get_option('systray')
  if x11_missing.length() == 0
    test(
      'systray',
      systray_test,
      args: [rawm, systray_client],
      workdir: test_workdir,
    )
  else
    test(
      'systray',
      skip_test,
      args: ['missing test dependencies: @0@'.format(', '.join(x11_missing))],
    )
  endif
endif

signals_missing = x11_missing
if not grep_program.found()
  signals_missing += ['grep']
endif
if signals_missing.length() == 0
  test(
    'signals',
    signals_test,
    args: [rawm],
    workdir: test_workdir,
  )
else
  test(
    'signals',
    skip_test,
    args: ['missing test dependencies: @0@'.format(', '.join(signals_missing))],
  )
endif

subdir('rawmstat')

subdir('unit')
