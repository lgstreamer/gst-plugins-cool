noinst_PROGRAMS = tsparser

tsparser_SOURCES = ts-parser.c
tsparser_CFLAGS = $(GST_PLUGINS_COOL_CFLAGS) $(GST_CFLAGS)
tsparser_LDFLAGS = $(GST_LIBS)
tsparser_LDADD = \
	$(top_builddir)/gst-libs/gst/dvrmpegts/libgstdvrmpegts-$(GST_API_VERSION).la
