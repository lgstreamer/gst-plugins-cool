dolbystreamcombiner_sources = [
  'gstdolbystreamcombiner.c',
  'plugin.c'
]

dolbystreamcombiner_deps = [
  gstcool_dep,
  gstaudio_dep,
  gstvideo_dep,
  gst_dep,
  gstbase_dep
]

gstdolbystreamcombiner = library('gstdolbystreamcombiner',
  dolbystreamcombiner_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dolbystreamcombiner_deps,
)
