decproxy_sources = [
  'gstdecbridge.c',
  'gstdecproxy2.c',
  'gstmediainfo.c',
  'plugin.c'
]

decproxy_deps = [
  gstcool_dep,
  gstaudio_dep,
  gstvideo_dep,
  gst_dep,
  gstbase_dep
]

gstdecproxy = library('gstdecproxy',
  decproxy_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : decproxy_deps,
)
