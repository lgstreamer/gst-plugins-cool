playready_sources = [
  'gstcencdrmplayready.c',
  'plugin.c'
]

playready_deps = [
  gstbase_dep,
  gst_dep,
  xml2_dep,
#FIXME: no dep for playready
  playready_dep,
  gstbasedrm_dep
]

gstcencdrmplayready = library('gstcencdrmplayready',
  playready_sources,
  c_args : gst_plugins_cool_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : playready_deps,
)
