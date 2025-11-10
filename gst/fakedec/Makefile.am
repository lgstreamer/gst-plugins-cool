plugin_LTLIBRARIES = libgstfakedec.la

# sources used to compile this plug-in
libgstfakedec_la_SOURCES = \
	gstfakeadec.c \
	gstfakevdec.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstfakedec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstfakedec_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstfakedec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfakedec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstfakeadec.h \
	gstfakevdec.h
