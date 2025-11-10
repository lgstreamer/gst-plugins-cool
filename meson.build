project('gst-plugins-cool', 'c',
  version : '1.14.4',
  meson_version : '>= 0.40.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif

have_cxx = add_languages('cpp', required : false)

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

orc_req = '>= 0.4.24'

#dnl required versions of playready-4.0
playready_req = '4.0.5102'

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor * 100 + gst_version_micro)

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      language : 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

cdata = configuration_data()
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MALLOC_H', 'malloc.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
# check token HAVE_CPU_ALPHA
# check token HAVE_CPU_ARM
# check token HAVE_CPU_CRIS
# check token HAVE_CPU_CRISV32
# check token HAVE_CPU_HPPA
# check token HAVE_CPU_I386
# check token HAVE_CPU_IA64
# check token HAVE_CPU_M68K
# check token HAVE_CPU_MIPS
# check token HAVE_CPU_PPC
# check token HAVE_CPU_PPC64
# check token HAVE_CPU_S390
# check token HAVE_CPU_SPARC
# check token HAVE_CPU_X86_64
  ['HAVE_DCGETTEXT', 'dcgettext'],
# check token HAVE_EXPERIMENTAL
# check token HAVE_EXTERNAL
# check token HAVE_GETTEXT
# check token HAVE_VALGRIND
# check token HAVE_LIBMMS
]

foreach f : check_functions
  if cc.has_function(f.get(1))
    cdata.set(f.get(0), 1)
  endif
endforeach

cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))

cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-plugins-cool"')
cdata.set('GETTEXT_PACKAGE', '"gst-plugins-cool-1.0"')
cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('PACKAGE_NAME', '"GStreamer Validate"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set_quoted('PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer')

# GStreamer package name and origin url
gst_package_name = get_option('with-package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer cool Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer cool Plug-ins git'
  else
    gst_package_name = 'GStreamer cool Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('with-package-origin'))

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_base_dep'])
gstriff_dep = dependency('gstreamer-riff-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'riff_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_net_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtsp_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstcodecparsers_dep = dependency('gstcodecparsers', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstcodecparsers_dep'])
gsturidownloader_dep = dependency('gsturidownloader', version : gst_req,
    fallback : ['gst-plugins-bad', 'gsturidownloader_dep'])
gstisoff_dep = dependency('gstisoff', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstisoff_dep'])
gstbasedrm_dep = dependency('gstbasedrm', version : gst_req,
    fallback : ['gst-plugins-bad', 'gstbasedrm_dep'])

if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
      fallback : ['gstreamer', 'gst_check_dep'])
endif
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])

# webOS deps
pmlog_dep = dependency('pmlog', required : false )
if pmlog_dep.found()
  cdata.set('USE_PMLOG', 1)
endif

# dnl check for playready
playready = get_option('use_playready')
if playready == 'yes'
  playready_dep = dependency('playready-4.0', version : playready_req, required : false)
  if playready_dep.found()
    have_playready = true
    cdata.set('HAVE_PLAYREADY', 1)
  else
    have_playready = false
  endif
endif

# FIXME: migrate the implementation of DEFAULT_PUPPET_AUDIO_FORMAT in configure.ac
cdata.set_quoted('DEFAULT_PUPPET_AUDIO_FORMAT', '"S32LE"')

# External deps
xml2_dep = dependency('libxml-2.0', version : '>= 2.8', required : false)
gio_dep = dependency('gio-2.0', fallback: ['glib', 'libgio_dep'])
zlib_dep = dependency('zlib', required : false)
libdl = cc.find_library('dl', required: false)
libm = cc.find_library('m', required : false)

# Used by the *_mkenum.py helper scripts
glib_mkenums = find_program('glib-mkenums')

have_orcc = false
orcc_args = []
if get_option('use_orc') != 'no'
  need_orc = get_option('use_orc') == 'yes'
  # Used by various libraries/elements that use Orc code
  orc_dep = dependency('orc-0.4', version : orc_req, required : need_orc)
  orcc = find_program('orcc', required : need_orc)
  if orc_dep.found() and orcc.found()
    have_orcc = true
    orcc_args = [orcc, '--include', 'glib.h']
    cdata.set('HAVE_ORC', 1)
  else
    message('Orc Compiler not found, will use backup C code')
    cdata.set('DISABLE_ORC', 1)
  endif
else
  cdata.set('DISABLE_ORC', 1)
endif

configure_file(output : 'config.h', configuration : cdata)

gst_plugins_cool_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

if gst_dep.type_name() == 'internal'
  gst_debug_disabled = subproject('gstreamer').get_variable('disable_gst_debug')
else
  # We can't check that in the case of subprojects as we won't
  # be able to build against an internal dependency (which is not built yet)
  gst_debug_disabled = cc.has_header_symbol('gst/gstconfig.h', 'GST_DISABLE_GST_DEBUG', dependencies: gst_dep)
endif

if gst_debug_disabled and cc.has_argument('-Wno-unused')
  add_project_arguments('-Wno-unused', language: 'c')
endif

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Wformat',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Waddress',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
  '-Waggregate-return',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Wdeclaration-after-statement',
  '-Wnested-externs'
]

if have_cxx
  cxx = meson.get_compiler('cpp')
endif

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
  if have_cxx and cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

gir = find_program('g-ir-scanner', required : false)
gnome = import('gnome')
build_gir = gir.found() and not meson.is_cross_build() and not get_option('disable_introspection')
gir_init_section = [ '--add-init-section=extern void gst_init(gint*,gchar**);' + \
    'g_setenv("GST_REGISTRY_DISABLE", "yes", TRUE);' + \
    'g_setenv("GST_REGISTRY_1.0", "@0@", TRUE);'.format(meson.current_build_dir() + '/gir_empty_registry.reg') + \
    'g_setenv("GST_PLUGIN_PATH_1_0", "", TRUE);' + \
    'g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", "", TRUE);' + \
    'gst_init(NULL,NULL);' ]

presetdir = join_paths(get_option('datadir'), 'gstreamer-' + api_version, 'presets')
subdir('gst-libs')
subdir('gst')
# FIXME: fix to support tests
#subdir('tests')

python3 = import('python3').find_python()
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
