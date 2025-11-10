httpextbin_sources = [
  'gsthttpextbin.c',
  'plugin.c'
]

httpextbin_deps = [
  gst_dep,
  gstaudio_dep,
  gstvideo_dep,
]

gsthttpextbni= library('gsthttpextbin',
  httpextbin_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : httpextbin_deps,
)
