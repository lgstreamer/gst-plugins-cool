uhdcp_sources = [
  'gstcencdrmuhdcp.c',
  'plugin.c'
]

uhdcp_deps = [
  gstbase_dep,
  gst_dep,
  gstbasedrm_dep
]

gstcencdrmuhdcp = library('gstcencdrmuhdcp',
  uhdcp_sources,
  c_args : gst_plugins_cool_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : uhdcp_deps,
)
