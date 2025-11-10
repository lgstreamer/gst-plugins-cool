
plugin_LTLIBRARIES = libgstdvrdashdemux.la

libgstdvrdashdemux_la_SOURCES =			\
	gstmpdparser.c				\
	gstdashdemux.c				\
	gstplugin.c				\
	gstadaptivedemux.c

# headers we need but don't want installed
noinst_HEADERS =        \
        gstmpdparser.h	\
	gstdashdemux.h	\
	gstdash_debug.h \
	adaptive-demux-prelude.h \
	gstadaptivedemux.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdvrdashdemux_la_CFLAGS = $(GST_PLUGINS_COOL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GIO_CFLAGS) \
	$(LIBXML2_CFLAGS) \
	-DGstDashDemux=GstDvrDashDemux -DGstDashDemuxClass=GstDvrDashDemuxClass
libgstdvrdashdemux_la_LIBADD = \
	-lgsturidownloader-@GST_API_VERSION@ \
	-lgstisoff-@GST_API_VERSION@ \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgsttag-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_NET_LIBS) \
	$(GST_LIBS) \
	$(GIO_LIBS) \
	$(LIBXML2_LIBS)
libgstdvrdashdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

