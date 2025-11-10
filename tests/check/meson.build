have_registry = true # FIXME not get_option('disable_registry')

# name, condition when to skip the test and extra dependencies
base_tests = [
  [['elements/dash_mpd.c'], not xml2_dep.found(), [xml2_dep]],
  [['elements/decbridge.c']],
  [['elements/decproxy.c']],
  [['elements/dynappsrc.c']],
  [['elements/httpextbin.c']],
  [['elements/qtdemux.c']],
  [['elements/splitappsink.c']],
  [['elements/streamiddemux.c']],
  [['elements/dash_demux.c', 'elements/test_http_src.c', 'elements/adaptive_demux_engine.c', 'elements/adaptive_demux_common.c']],
]

test_defines = [
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
  '-DGST_CHECK_TEST_ENVIRONMENT_BEACON="GST_STATE_IGNORE_ELEMENTS"',
  '-DGST_TEST_FILES_PATH="' + meson.current_source_dir() + '/../files"',
  '-DTEST_PATH="' + meson.current_build_dir() + '/media"',
  '-DDASH_MPD_DATADIR=' + meson.current_source_dir() + '/elements/dash_mpd_data',
  '-DGST_USE_UNSTABLE_API',
]

test_deps = [gst_dep, gstapp_dep, gstbase_dep,
             gstpbutils_dep, gstcontroller_dep, gstaudio_dep,
             gstvideo_dep, gstrtp_dep, gsturidownloader_dep,
             gstcheck_dep, gio_dep, gsttag_dep]

pluginsdirs = [  ]

if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version)

  pluginsdirs = [gst_dep.get_pkgconfig_variable('pluginsdir')] + [pbase.get_pkgconfig_variable('pluginsdir')]
endif

foreach t : base_tests
  fnames = t.get(0)
  test_name = fnames[0].split('.').get(0).underscorify()
  skip_test = false
  extra_deps = [ ]

  if t.length() >= 3
    extra_deps = t.get(2)
  endif

  if t.length() >= 2
    skip_test = t.get(1)
  endif

  if not skip_test
    exe = executable(test_name, fnames,
      include_directories : [configinc],
      c_args : ['-DHAVE_CONFIG_H=1' ] + test_defines,
      cpp_args : gst_plugins_cool_args,
      dependencies : [libm] + test_deps + extra_deps,
    )

    env = environment()
    env.set('GST_PLUGIN_PATH_1_0', meson.build_root())
    env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
    env.set('CK_DEFAULT_TIMEOUT', '20')
    env.set('GST_STATE_IGNORE_ELEMENTS', '')
    env.set('GST_PLUGIN_PATH_1_0', [meson.build_root()] + pluginsdirs)
    env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), test_name))
    test(test_name, exe, env: env, timeout: 3 * 60)
  endif
endforeach
