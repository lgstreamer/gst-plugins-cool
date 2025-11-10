dvrsiomp4_sources = [
  'isomp4-plugin.c',
  'qtdemux.c',
  'qtdemux_types.c',
  'qtdemux_dump.c',
  'qtdemux_lang.c',
  'descriptors.c',
  'properties.c',
  'gstisoff.c'
]

dvrsiomp4_deps = [
  gstbase_dep,
  gst_dep,
  gstriff_dep,
  gstaudio_dep,
  gstvideo_dep,
  gstrtp_dep,
  gsttag_dep,
  gstpbutils_dep,
  zlib_dep
]

dvrsiomp4_args = [
  '-DGstQTDemux=GstDvrQTDemux',
  '-DGstQTDemuxClass=GstDvrQTDemuxClass'
]

gstdvrisomp4 = library('gstdvrisomp4',
  dvrsiomp4_sources,
  c_args : gst_plugins_cool_args + dvrsiomp4_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dvrsiomp4_deps,
)
