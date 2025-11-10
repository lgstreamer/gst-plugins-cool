reformatter_sources = [
  'gstreformatter.c',
  'plugin.c'
]

reformatter_deps = [
  gst_dep,
  gstbase_dep,
  gstcool_dep,
  gstaudio_dep,
  gstvideo_dep,
]

gstreformatter= library('gstreformatter',
  reformatter_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : reformatter_deps,
)
