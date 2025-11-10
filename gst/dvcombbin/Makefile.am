plugin_LTLIBRARIES = libgstdvcombbin.la

# sources used to compile this plug-in
libgstdvcombbin_la_SOURCES = \
	gstdvcombbin.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdvcombbin_la_CFLAGS = $(GST_CFLAGS)
libgstdvcombbin_la_LIBADD = $(GST_LIBS)
libgstdvcombbin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvcombbin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstdvcombbin.h
