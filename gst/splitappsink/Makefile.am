plugin_LTLIBRARIES = libgstsplitappsink.la

# sources used to compile this plug-in
libgstsplitappsink_la_SOURCES = \
	gstsplitappsink.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstsplitappsink_la_CFLAGS = $(GST_CFLAGS)
libgstsplitappsink_la_LIBADD = $(GST_LIBS)
libgstsplitappsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsplitappsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstsplitappsink.h
