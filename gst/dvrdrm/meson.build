dvrdrm_sources = [
  'gstdvrdrmdecrypt.c',
  'gstdvrdrmencrypt.c',
  'dvrdrm-plugin.c'
]

dvrdrm_deps = [
  gstbase_dep,
  gst_dep
]

gstdvrdrm = library('gstdvrdrm',
  dvrdrm_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dvrdrm_deps,
)
