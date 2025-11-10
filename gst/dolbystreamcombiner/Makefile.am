plugin_LTLIBRARIES = libgstdolbystreamcombiner.la

libgstdolbystreamcombiner_la_SOURCES = 	\
	gstdolbystreamcombiner.c \
	plugin.c

libgstdolbystreamcombiner_la_CFLAGS = $(GST_CFLAGS)
libgstdolbystreamcombiner_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdolbystreamcombiner_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la \
	-lgstaudio-$(GST_API_VERSION) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstdolbystreamcombiner_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = 		\
	gstdolbystreamcombiner.h
