dvrdashdemux_sources = [
  'gstmpdparser.c',
  'gstdashdemux.c',
  'gstplugin.c',
  'gstadaptivedemux.c'
]

dvrdashdemux_deps = [
  gsturidownloader_dep,
  gstisoff_dep,
  gstbase_dep,
  gsttag_dep,
  gstnet_dep,
  gst_dep,
  gio_dep,
  xml2_dep
]

dvrdashdemux_args = [
  '-DGstDashDemux=GstDvrDashDemux',
  '-DGstDashDemuxClass=GstDvrDashDemuxClass',
  '-DGST_USE_UNSTABLE_API'
]

gstdvrdashdemux = library('gstdvrdashdemux',
  dvrdashdemux_sources,
  c_args : gst_plugins_cool_args + dvrdashdemux_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dvrdashdemux_deps,
)
