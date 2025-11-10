cool_sources = [
  'gstcool.c',
  'gstcoolplaybin.c',
  'gstcoolutil.c'
]

cool_headers = [
  'gstcool.h',
  'gstcoolplaybin.h',
  'gstcoolutil.h',
  'gstcoolrawcaps.h'
]

cool_deps = [
  gstbase_dep,
  gstaudio_dep
]

install_headers(cool_headers, subdir : 'gstreamer-1.0/gst/cool/')

gstcool = library('gstcool-' + api_version,
  cool_sources,
  c_args : gst_plugins_cool_args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : cool_deps
)

cool_gen_sources  = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/cool/gstcool.h' ]
  cool_gen_sources += [gnome.generate_gir(gstcool,
    sources : cool_sources + cool_headers,
    namespace : 'GstCool',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-cool-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : cool_deps
  )]
endif

gstcool_dep = declare_dependency(link_with : gstcool,
  include_directories : [libsinc],
  dependencies : cool_deps,
  sources : cool_gen_sources)
