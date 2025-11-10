
plugin_LTLIBRARIES = libgstdvrisomp4.la

libgstdvrisomp4_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-DGstQTDemux=GstDvrQTDemux -DGstQTDemuxClass=GstDvrQTDemuxClass
libgstdvrisomp4_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgstriff-@GST_API_VERSION@ \
    -lgstaudio-@GST_API_VERSION@ \
    -lgstvideo-@GST_API_VERSION@ \
    -lgstrtp-@GST_API_VERSION@ \
    -lgsttag-@GST_API_VERSION@ \
    -lgstpbutils-@GST_API_VERSION@ \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgstdvrisomp4_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstdvrisomp4_la_SOURCES = isomp4-plugin.c \
	qtdemux.c qtdemux_types.c qtdemux_dump.c qtdemux_lang.c \
	descriptors.c properties.c gstisoff.c
libgstdvrisomp4_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	qtatomparser.h   \
	qtdemux.h        \
	qtdemux_types.h  \
	qtdemux_debug.h  \
	qtdemux_dump.h   \
	qtdemux_lang.h   \
	qtpalette.h      \
	descriptors.h    \
	properties.h     \
	fourcc.h         \
	gstisoff.h
