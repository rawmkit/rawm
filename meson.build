project(
  'rawm',
  'c',
  version: '6.0',
  meson_version: '>=1.4.0',
  default_options: [
    'c_std=c99',
    'warning_level=3',
    'sysconfdir=/etc',
  ],
)


add_project_arguments(
  '-D_DEFAULT_SOURCE',
  '-D_BSD_SOURCE',
  '-D_POSIX_C_SOURCE=200809L',
  language: 'c',
)

x11_dep = dependency('x11')
xft_dep = dependency('xft')
fontconfig_dep = dependency('fontconfig')
libconfig_dep = dependency('libconfig')

rawm_deps = [
  x11_dep,
  xft_dep,
  fontconfig_dep,
  libconfig_dep,
]
rawm_c_args = []

xinerama_dep = dependency('xinerama', required: get_option('xinerama'))
if xinerama_dep.found()
  rawm_deps += xinerama_dep
  rawm_c_args += '-DXINERAMA'
endif

if get_option('systray')
  rawm_c_args += '-DSYSTRAY'
endif

if get_option('per_window_keyboard_layout')
  rawm_c_args += '-DPWKL'
endif

if get_option('window_title')
  rawm_c_args += '-DWINTITLE'
endif

sysconfdir = get_option('sysconfdir')
runtime_sysconfdir = sysconfdir
if not runtime_sysconfdir.startswith('/')
  runtime_sysconfdir = join_paths(get_option('prefix'), runtime_sysconfdir)
endif

rawm_c_args += [
  '-DVERSION="@0@"'.format(meson.project_version()),
  '-DSYSCONFDIR="@0@"'.format(runtime_sysconfdir),
]

subdir('src')

scdoc = find_program('scdoc')

custom_target(
  'rawm.1',
  input: 'rawm.1.scdoc',
  output: 'rawm.1',
  command: [scdoc],
  feed: true,
  capture: true,
  install: true,
  install_dir: join_paths(get_option('mandir'), 'man1'),
)

custom_target(
  'rawm.conf.5',
  input: 'rawm.conf.5.scdoc',
  output: 'rawm.conf.5',
  command: [scdoc],
  feed: true,
  capture: true,
  install: true,
  install_dir: join_paths(get_option('mandir'), 'man5'),
)

install_data(
  'rawm.conf',
  install_dir: join_paths(get_option('sysconfdir'), 'rawm'),
)

subdir('tests')
