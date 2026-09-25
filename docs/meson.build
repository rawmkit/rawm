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

custom_target(
  'rawmstat.1',
  input: 'rawmstat.1.scdoc',
  output: 'rawmstat.1',
  command: [scdoc],
  feed: true,
  capture: true,
  install: true,
  install_dir: join_paths(get_option('mandir'), 'man1'),
)

custom_target(
  'rawmstat.conf.5',
  input: 'rawmstat.conf.5.scdoc',
  output: 'rawmstat.conf.5',
  command: [scdoc],
  feed: true,
  capture: true,
  install: true,
  install_dir: join_paths(get_option('mandir'), 'man5'),
)
