rawmctl = executable(
  'rawmctl',
  [
    'main.c',
    'window.c',
  ],
  c_args: ['-DVERSION="@0@"'.format(meson.project_version())],
  dependencies: x11_dep,
  install: true,
)
