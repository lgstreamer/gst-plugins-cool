dynappsrc_sources = [
  'gstdynamic.c',
  'gstdynappsrc.c'
]

dynappsrc_deps = [
  gst_dep,
  gstaudio_dep,
  gstvideo_dep,
]

gstdynappsrc = library('gstdynappsrc',
  dynappsrc_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dynappsrc_deps,
)
