plugin_LTLIBRARIES = libgstavibin.la

# sources used to compile this plug-in
libgstavibin_la_SOURCES = gstavibin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstavibin_la_CFLAGS = $(GST_CFLAGS)
libgstavibin_la_LIBADD = $(GST_LIBS)
libgstavibin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstavibin_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS =  gstavibin.h
