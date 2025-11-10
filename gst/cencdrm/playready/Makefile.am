plugin_LTLIBRARIES = libgstcencdrmplayready.la

# sources used to compile this plug-in
libgstcencdrmplayready_la_SOURCES = \
	gstcencdrmplayready.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstcencdrmplayready_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(LIBXML2_CFLAGS) \
	$(PLAYREADY_CFLAGS)

libgstcencdrmplayready_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_PLUGINS_BAD_LIBS) \
	$(LIBXML2_LIBS) \
	$(PLAYREADY_LIBS) \
	-lgstbasedrm-$(GST_API_VERSION)

libgstcencdrmplayready_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcencdrmplayready_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstcencdrmplayready.h
