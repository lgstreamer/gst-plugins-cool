marlin_sources = [
  'gstcencdrmmarlin.c',
  'plugin.c'
]

marlin_deps = [
  gstbase_dep,
  gst_dep,
  gstbasedrm_dep
]

gstcencdrmmarlin = library('gstcencdrmmarlin',
  marlin_sources,
  c_args : gst_plugins_cool_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : marlin_deps
)
