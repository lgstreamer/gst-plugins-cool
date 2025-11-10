plugin_LTLIBRARIES = libgstcencdrmmarlin.la

# sources used to compile this plug-in
libgstcencdrmmarlin_la_SOURCES = \
	gstcencdrmmarlin.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstcencdrmmarlin_la_CFLAGS = $(GST_CFLAGS)

libgstcencdrmmarlin_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_PLUGINS_BAD_LIBS) \
	-lgstbasedrm-$(GST_API_VERSION)

libgstcencdrmmarlin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcencdrmmarlin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstcencdrmmarlin.h
