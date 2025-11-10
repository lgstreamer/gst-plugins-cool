fakedec_sources = [
  'gstfakeadec.c',
  'gstfakevdec.c',
  'plugin.c'
]

fakedec_deps = [
  gst_dep,
  gstbase_dep,
  gstcool_dep,
  gstaudio_dep,
  gstvideo_dep,
]

gstfakedec = library('gstfakedec',
  fakedec_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : fakedec_deps,
)
