dashsink_sources = [
  'plugin.cpp',
  'gstdashsink.cpp',
  'xmlelement.cpp',
  'manifestgenerator.cpp'
]

dashsink_deps = [
  gstbase_dep,
  gsttag_dep,
  gst_dep,
  xml2_dep
]

gstdashsink = library('gstdashsink',
  dashsink_sources,
  cpp_args : gst_plugins_cool_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dashsink_deps,
)
