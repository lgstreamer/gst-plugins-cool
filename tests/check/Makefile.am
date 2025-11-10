include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg
TEST_FILES_DIRECTORY = $(top_srcdir)/tests/files

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY_1_0=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        CK_DEFAULT_TIMEOUT=120                                  \
        GST_STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH_1_0=				\
	GST_PLUGIN_PATH_1_0=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(GST_PLUGINS_GOOD_DIR):$(GST_PLUGINS_BAD_DIR):$(GST_PLUGINS_BASE_DIR):$(GST_PLUGINS_DIR) \
	GST_PLUGIN_LOADING_WHITELIST="gstreamer@$(GST_PLUGINS_DIR):gst-plugins-base@$(GSTPB_PLUGINS_DIR):gst-plugins-good:gst-plugins-bad:gst-plugins-cool@$(top_builddir)" \
	GST_TAG_LICENSE_TRANSLATIONS_DICT="$(top_srcdir)/gst-libs/gst/tag/license-translations.dict"

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)

# elements to ignore for the state tests
STATE_IGNORE_ELEMENTS =

TESTS = $(check_PROGRAMS)

check_PROGRAMS = \
	elements/decbridge \
	elements/decproxy \
	elements/streamiddemux \
	elements/dynappsrc \
	elements/splitappsink \
	elements/dash_mpd \
	elements/qtdemux \
	cool/gstcool \
	cool/gstcoolutil \
	libs/mpegts

# these tests don't even pass
noinst_PROGRAMS = \
	elements/dash_demux \
	elements/qtdemux \
	libs/mpegts

noinst_HEADERS =

AM_CFLAGS = -I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS

AM_CXXFLAGS = -I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
	$(GST_CXXFLAGS) $(GST_CHECK_CFLAGS) \
	-DGST_TEST_FILES_PATH="\"$(TEST_FILES_DIRECTORY)\"" \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS

LDADD = $(GST_LIBS) $(GST_CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE = $(VALGRIND_TO_FIX)

elements_decbridge_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_decproxy_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_streamiddemux_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
        $(AM_CFLAGS)

elements_streamiddemux_LDADD = \
	$(LDADD)

elements_dynappsrc_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

elements_dynappsrc_LDADD = \
	$(LDADD)

elements_splitappsink_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
        $(AM_CFLAGS)

elements_splitappsink_LDADD = \
	$(LDADD)

elements_dash_mpd_CFLAGS = $(GST_BASE_CFLAGS) $(AM_CFLAGS) $(GST_PLUGINS_COOL_CFLAGS) $(LIBXML2_CFLAGS) \
	-DDASH_MPD_DATADIR="$(srcdir)/elements/dash_mpd_data"
elements_dash_mpd_LDADD = $(GST_BASE_LIBS) $(LDADD) $(LIBXML2_LIBS) \
	-lgsturidownloader-@GST_API_VERSION@ \
elements_dash_mpd_SOURCES = elements/dash_mpd.c


elements_dash_demux_CFLAGS = $(GST_PLUGINS_COOL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(AM_CFLAGS) $(LIBXML2_CFLAGS)
elements_dash_demux_LDADD = \
	-lgsturidownloader-@GST_API_VERSION@ \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(LIBXML2_LIBS) $(LDADD)

elements_dash_demux_SOURCES = elements/test_http_src.c elements/test_http_src.h elements/adaptive_demux_engine.c elements/adaptive_demux_engine.h elements/adaptive_demux_common.c elements/adaptive_demux_common.h elements/dash_demux.c

elements_qtdemux_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AM_CFLAGS)
elements_qtdemux_LDADD = $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GST_CHECK_LIBS)
elements_qtdemux_SOURCES = elements/qtdemux.h elements/qtdemux.c

cool_gstcool_CFLAGS = \
        -DGST_COOL_CONFIG_PATH=\""$(top_builddir)/config/m14tv/gstcool.conf\"" \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

cool_gstcool_LDADD = \
        $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la \
        $(LDADD)

cool_gstcoolutil_CFLAGS = \
        -DGST_COOL_CONFIG_PATH=\""$(top_builddir)/config/m14tv/gstcool.conf\"" \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(AM_CFLAGS)

cool_gstcoolutil_LDADD = \
        $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la \
        $(LDADD)

libs_mpegts_CFLAGS = \
	$(GST_PLUGINS_COOL_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS) \
	$(PTHREAD_CFLAGS)

libs_mpegts_LDADD = \
	$(top_builddir)/gst-libs/gst/dvrmpegts/libgstdvrmpegts-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) $(LDADD)

EXTRA_DIST = \
		elements/qtdemux.h