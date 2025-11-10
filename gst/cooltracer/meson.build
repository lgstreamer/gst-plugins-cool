cooltracer_sources = [
  'gsttraceelement.c',
  'gsttracepad.c',
  'gsttraceprocstat.c',
  'gstcooltracer.c',
  'gstpmlog.c',
  'plugin.c'
]

cooltracer_deps = [
  gstbase_dep,
  gst_dep,
  pmlog_dep
]

gstcooltracer = library('gstcooltracer',
  cooltracer_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : cooltracer_deps,
)
