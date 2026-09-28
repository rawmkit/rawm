status_codec_test = executable(
  'status-codec-test',
  'status-codec-test.c',
  dependencies: rawm_status_dep,
  build_by_default: false,
)

test('status-codec', status_codec_test)

alsa_headers_dep = alsa_dep.partial_dependency(
  compile_args: true,
  includes: true,
)

status_alsa_test = executable(
  'status-alsa-test',
  'status-alsa-test.c',
  '../../src/status/alsa.c',
  include_directories: rawm_src_include,
  dependencies: [alsa_headers_dep, rawm_status_dep],
  build_by_default: false,
)

test('status-alsa', status_alsa_test)
