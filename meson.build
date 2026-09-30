project(
  'rawm',
  'c',
  version: '7.0.0',
  meson_version: '>=1.6.0',
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
libconfig_dep = dependency('libconfig')

rawm_deps = [
  x11_dep,
  libconfig_dep,
]
rawm_c_args = []

xinerama_dep = dependency('xinerama', required: get_option('xinerama'))
if xinerama_dep.found()
  rawm_deps += xinerama_dep
  rawm_c_args += '-DXINERAMA'
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

subdir('docs')

subdir('config')
subdir('tests')
