splitappsink_sources = [
  'gstsplitappsink.c',
  'plugin.c'
]

gstsplitappsink= library('gstsplitappsink',
  splitappsink_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : gst_dep,
)
