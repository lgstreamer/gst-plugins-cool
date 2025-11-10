dnl required version of autoconf
AC_PREREQ([2.62])

dnl TODO: fill in your package name and package version here
AC_INIT(Gstreamer Cool Plug-ins,1.0.0,
https://github.com/altura/gst-plugins-cool,
gst-plugins-cool)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar])

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.2.0
GSTPB_REQ=1.2.0

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

dnl required versions of playready-4.0
PLAYREADY_REQUIRED=4.0.5102

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/decproxy/gstdecproxy2.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl GStreamer versioning, this is mostly informational
GST_VERSION_MAJOR=$PACKAGE_VERSION_MAJOR
GST_VERSION_MINOR=$PACKAGE_VERSION_MINOR
GST_VERSION_MICRO=$PACKAGE_VERSION_MICRO
GST_VERSION_NANO=$PACKAGE_VERSION_NANO
AC_SUBST(GST_VERSION_MAJOR)
AC_SUBST(GST_VERSION_MINOR)
AC_SUBST(GST_VERSION_MICRO)
AC_SUBST(GST_VERSION_NANO)

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 9, 0, 9)

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal (keep in sync with Makefile.am)
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AG_GST_GETTEXT([gst-plugins-cool-$GST_API_VERSION])

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN
AG_GST_ARG_WITH_PLUGINS

AG_GST_PKG_CONFIG_PATH

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl determine c++ compiler
AC_PROG_CXX
CXXFLAGS="$CXXFLAGS -std=c++0x"

dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

dnl *** checks for libraries ***

dnl libm, for sin() etc.
LT_LIB_M
AC_SUBST(LIBM)

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl GLib is required
GLIB_REQ=2.40.0
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_NET($GST_API_VERSION, [$GST_REQ], yes)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GSTPB_REQ], yes)

dnl check for uninstalled plugin directories for unit tests
AG_GST_CHECK_UNINSTALLED_SETUP([
  AG_GST_CHECK_GST_PLUGINS_GOOD($GST_API_VERSION, [0.11.0])
])
AG_GST_CHECK_GST_PLUGINS_BAD($GST_API_VERSION, [$GSTPB_REQ], yes)

AG_GST_CHECK_FEATURE(SOUP, [soup http client plugin (2.4)], souphttpsrc, [
  PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.40, [HAVE_SOUP="yes"], [HAVE_SOUP="no"])
  AC_SUBST(SOUP_CFLAGS)
  AC_SUBST(SOUP_LIBS)
])

AG_GST_CHECK_FEATURE(LIBXML2, [dash sink plugin (2.4)], dashsink, [
  PKG_CHECK_MODULES(LIBXML2, libxml-2.0 >= 2.4, [HAVE_LIBXML2="yes"], [HAVE_LIBXML2="no"])
  AC_SUBST(LIBXML2_CFLAGS)
  AC_SUBST(LIBXML2_LIBS)
])

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
GSTPB_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-plugins-base-$GST_API_VERSION`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)
AC_SUBST(GSTPB_PREFIX)

AG_GST_CHECK_PLUGIN(decproxy)
AG_GST_CHECK_PLUGIN(httpextbin)
AG_GST_CHECK_PLUGIN(dynappsrc)
AG_GST_CHECK_PLUGIN(tsinkbin)
AG_GST_CHECK_PLUGIN(textbin)
AG_GST_CHECK_PLUGIN(fakedec)
AG_GST_CHECK_PLUGIN(dolbystreamcombiner)
AG_GST_CHECK_PLUGIN(dvcombbin)
AG_GST_CHECK_PLUGIN(cencdrm)
AG_GST_CHECK_PLUGIN(cooltracer)
AG_GST_CHECK_PLUGIN(splitappsink)
AG_GST_CHECK_PLUGIN(dvrdrm)
AG_GST_CHECK_PLUGIN(dashsink)
AG_GST_CHECK_PLUGIN(avibin)
AG_GST_CHECK_PLUGIN(mpomux)
AG_GST_CHECK_PLUGIN(dvrdashdemux)
AG_GST_CHECK_PLUGIN(dvrisomp4)
AG_GST_CHECK_PLUGIN(dvrtsdemux)

dnl check for plugins to enable or disable
AG_GST_CHECK_PLUGIN_WITH_DEFAULT_FEATURE(reformatter, no)
AG_GST_CHECK_PLUGIN_WITH_DEFAULT_FEATURE(streamiddemux, no)

dnl check for which audio format is used for puppet
AC_ARG_WITH(puppet-audio-format,
  AC_HELP_STRING([--with-puppet-audio-format], [Which audio format is used for puppet, S16LE/S32LE, (default is S32LE)]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-puppet-audio-format) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-puppet-audio-format) ;;
      *)   DEFAULT_PUPPET_AUDIO_FORMAT="${withval}" ;;
    esac
  ],
  [
    DEFAULT_PUPPET_AUDIO_FORMAT="S32LE"
  ] dnl Default value
)
AC_MSG_NOTICE(Using $DEFAULT_PUPPET_AUDIO_FORMAT as default audio format for puppet)
AC_SUBST(DEFAULT_PUPPET_AUDIO_FORMAT)
AC_DEFINE_UNQUOTED(DEFAULT_PUPPET_AUDIO_FORMAT, "$DEFAULT_PUPPET_AUDIO_FORMAT",
  [Default audio format for puppet])


dnl check for documentation tools
GTK_DOC_CHECK([1.12])

dnl GTK is optional and only used in examples
HAVE_GTK=no
HAVE_GTK_X11=no

AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_X11, test "x$HAVE_GTK_X11" = "xyes")

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
gstreamer-1.0 >= $GST_REQUIRED
gstreamer-base-1.0 >= $GST_REQUIRED
], [
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)
], [
AC_MSG_ERROR([
You need to install or upgrade the GStreamer development
packages on your system. On debian-based systems these are
libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
or similar. The minimum version required is $GST_REQUIRED.
])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
GST_CFLAGS="$GST_CFLAGS -Wall"
AC_MSG_RESULT([yes])
], [
AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
plugindir="$HOME/.gstreamer-1.0/plugins"
else
plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl every flag in GST_OPTION_CFLAGS, GST_OPTION_CXXFLAGS and GST_OPTION_OBJCFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_OBJCFLAGS="\$(WARNING_OBJCFLAGS) \$(ERROR_OBJCFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)
AC_SUBST(GST_OPTION_OBJCFLAGS)

dnl our libraries need to be versioned correctly
AC_SUBST(GST_LT_LDFLAGS)

AC_CHECK_HEADER(PmLogLib.h, [
HAVE_PMLOG_HEADER=yes
GST_CFLAGS="$GST_CFLAGS -DUSE_PMLOG"
PMLOG_LIBS="-lPmLogLib"
AC_SUBST(PMLOG_LIBS)], [
AC_MSG_NOTICE([Could not find PmLogLib header. Cooltracer will use stdout instead of PmLog.])
], [
])

VISIBILITY_CFLAGS=""
AS_COMPILER_FLAG([-fvisibility=hidden], [VISIBILITY_CFLAGS="-fvisibility=hidden"])
AC_SUBST(VISIBILITY_CFLAGS)

VISIBILITY_CXXFLAGS=""
if test "x$HAVE_CXX" = "xyes"; then
  AS_CXX_COMPILER_FLAG([-fvisibility=hidden], [VISIBILITY_CXXFLAGS="-fvisibility=hidden"])
fi
AC_SUBST(VISIBILITY_CXXFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add ERROR_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CXXFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS) \$(VISIBILITY_CXXFLAGS)"
GST_OBJCFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_OBJCFLAGS)"
GST_CFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS) \$(VISIBILITY_CXXFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
AC_SUBST(GST_OBJCFLAGS)
AC_SUBST(GST_LIBS)
GST_PLUGINS_COOL_CXXFLAGS="-I\$(top_builddir)/gst-libs"
GST_PLUGINS_COOL_OBJCFLAGS="-I\$(top_builddir)/gst-libs"
GST_PLUGINS_COOL_CFLAGS="-I\$(top_builddir)/gst-libs"
AC_SUBST(GST_PLUGINS_COOL_CFLAGS)
AC_SUBST(GST_PLUGINS_COOL_CXXFLAGS)
AC_SUBST(GST_PLUGINS_COOL_OBJCFLAGS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_ARG_WITH([tmpdir],
  AS_HELP_STRING([--with-tmpdir],[tmp dir]),
  [
    TMPDIR="${withval}"
  ], [
    TMPDIR="/var/tmp"
  ]
)
AC_SUBST(TMPDIR)

AC_MSG_NOTICE([Using ${MACHINE} as a target])
case "${MACHINE}" in
  h15|k2h|k2l|lm15u|m14tv|mtka5lr|rtks2b|m16|m2|m16p|m2r|k3l|k3lp|k4lp|m16pp|o18|m3|lm18a|k5lp|k5ap|m3r|m16p3|lm19a|o20|o208k|lm20a|lm20u|k6lp|k6hp)
    HAVE_MACHINE=yes
    ;;
  qemux86)
    MACHINE=emul
    HAVE_MACHINE=no
    ;;
  none|*)
    MACHINE=default
    HAVE_MACHINE=no
    ;;
esac

MACHINE_CONF_DATA="${MACHINE}/gstcool.conf"
AC_SUBST(MACHINE_CONF_DATA)
AC_MSG_NOTICE([machine conf data : ${MACHINE_CONF_DATA}])

dnl check for playready
translit(dnm, m, l) AM_CONDITIONAL(USE_PLAYREADY, true)
AG_GST_CHECK_FEATURE(PLAYREADY, [PLAYREADY plug-in], playready, [
  PKG_CHECK_MODULES(PLAYREADY, playready-4.0 >= $PLAYREADY_REQUIRED, [
    HAVE_PLAYREADY="yes"], [
    HAVE_PLAYREADY="no"])
  AC_SUBST(PLAYREADY_CFLAGS)
  AC_SUBST(PLAYRADY_LIBS)
])

AM_CONDITIONAL([HAVE_MACHINE], [test "x${HAVE_MACHINE}" = "xyes"])
AM_CONDITIONAL([PLATFORM_H15], [test "x${MACHINE}" = "xh15"])
AM_CONDITIONAL([PLATFORM_M16], [test "x${MACHINE}" = "xm16"])
AM_CONDITIONAL([PLATFORM_LM15U], [test "x${MACHINE}" = "xlm15u"])
AM_CONDITIONAL([PLATFORM_LM14A], [test "x${MACHINE}" = "xlm14a"])
AM_CONDITIONAL([PLATFORM_M2], [test "x${MACHINE}" = "xm2"])
AM_CONDITIONAL([PLATFORM_K2H], [test "x${MACHINE}" = "xk2h"])
AM_CONDITIONAL([PLATFORM_K2L], [test "x${MACHINE}" = "xk2l"])
AM_CONDITIONAL([PLATFORM_M16P], [test "x${MACHINE}" = "xm16p"])
AM_CONDITIONAL([PLATFORM_K3LP], [test "x${MACHINE}" = "xk3lp"])
AM_CONDITIONAL([PLATFORM_M2R], [test "x${MACHINE}" = "xm2r"])
AM_CONDITIONAL([PLATFORM_K4LP], [test "x${MACHINE}" = "xk4lp"])
AM_CONDITIONAL([PLATFORM_M16PP], [test "x${MACHINE}" = "xm16pp"])
AM_CONDITIONAL([PLATFORM_O18], [test "x${MACHINE}" = "xo18"])
AM_CONDITIONAL([PLATFORM_M3], [test "x${MACHINE}" = "xm3"])
AM_CONDITIONAL([PLATFORM_LM18A], [test "x${MACHINE}" = "xlm18a"])
AM_CONDITIONAL([PLATFORM_K5LP], [test "x${MACHINE}" = "xk5lp"])
AM_CONDITIONAL([PLATFORM_K5AP], [test "x${MACHINE}" = "xk5ap"])
AM_CONDITIONAL([PLATFORM_M3R], [test "x${MACHINE}" = "xm3r"])
AM_CONDITIONAL([PLATFORM_M16P3], [test "x${MACHINE}" = "xm16p3"])
AM_CONDITIONAL([PLATFORM_LM19A], [test "x${MACHINE}" = "xlm19a"])
AM_CONDITIONAL([PLATFORM_O20], [test "x${MACHINE}" = "xo20"])
AM_CONDITIONAL([PLATFORM_O208K], [test "x${MACHINE}" = "xo208k"])
AM_CONDITIONAL([PLATFORM_LM20A], [test "x${MACHINE}" = "xlm20a"])
AM_CONDITIONAL([PLATFORM_LM20U], [test "x${MACHINE}" = "xlm20u"])
AM_CONDITIONAL([PLATFORM_K6LP], [test "x${MACHINE}" = "xk6lp"])
AM_CONDITIONAL([PLATFORM_K6HP], [test "x${MACHINE}" = "xk6hp"])

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
config/Makefile
gst/Makefile
gst/decproxy/Makefile
gst/fakedec/Makefile
gst/httpextbin/Makefile
gst/dynappsrc/Makefile
gst/streamiddemux/Makefile
gst/textbin/Makefile
gst/reformatter/Makefile
gst/tsinkbin/Makefile
gst/dolbystreamcombiner/Makefile
gst/dvcombbin/Makefile
gst/cencdrm/Makefile
gst/cencdrm/marlin/Makefile
gst/cencdrm/playready/Makefile
gst/cencdrm/uhdcp/Makefile
gst/dvrdrm/Makefile
gst/cooltracer/Makefile
gst/splitappsink/Makefile
gst/dashsink/Makefile
gst/avibin/Makefile
gst/mpomux/Makefile
gst/dvrdashdemux/Makefile
gst/dvrisomp4/Makefile
gst/dvrtsdemux/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/cool/Makefile
gst-libs/gst/dvrmpegts/Makefile
pkgconfig/Makefile
pkgconfig/gstreamer-cool.pc
pkgconfig/gstreamer-cool-uninstalled.pc
pkgconfig/gstreamer-dvrmpegts.pc
pkgconfig/gstreamer-dvrmpegts-uninstalled.pc
m4/Makefile
scripts/registry/gst-registry.conf
scripts/registry/gst-env.conf
)
AC_OUTPUT
