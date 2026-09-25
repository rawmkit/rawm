rawmstat_version = '0.1'
rawmstat_c_args = [
  '-DVERSION="@0@"'.format(rawmstat_version),
  '-DSYSCONFDIR="@0@"'.format(runtime_sysconfdir),
]

rawmstat = executable(
  'rawmstat',
  [
    'rawmstat.c',
    'rawmstat-config.c',
  ],
  c_args: rawmstat_c_args,
  dependencies: [x11_dep, libconfig_dep],
  install: true,
)
