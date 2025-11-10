plugin_LTLIBRARIES = libgstcencdrmwidevine.la

# sources used to compile this plug-in
libgstcencdrmwidevine_la_SOURCES = \
	gstcencdrmwidevine.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstcencdrmwidevine_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(WIDEVINE_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(JSONC_CFLAGS)

libgstcencdrmwidevine_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_PLUGINS_BAD_LIBS) \
	$(WIDEVINE_LIBS) \
	$(SOUP_LIBS) \
	$(JSONC_LIBS) \
	-lgstbasedrm-$(GST_API_VERSION)

libgstcencdrmwidevine_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcencdrmwidevine_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstcencdrmwidevine.h
