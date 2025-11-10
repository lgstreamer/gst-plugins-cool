plugin_LTLIBRARIES = libgstmpomux.la

# sources used to compile this plug-in
libgstmpomux_la_SOURCES = gstmpomux.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstmpomux_la_CFLAGS = $(GST_CFLAGS)
libgstmpomux_la_LIBADD = $(GST_LIBS)
libgstmpomux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpomux_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS =  gstmpomux.h
