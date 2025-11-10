plugin_LTLIBRARIES = libgstdvrdrm.la

# sources used to compile this plug-in
libgstdvrdrm_la_SOURCES = \
	gstdvrdrmdecrypt.c \
	gstdvrdrmencrypt.c \
	dvrdrm-plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdvrdrm_la_CFLAGS = $(GST_CFLAGS)

libgstdvrdrm_la_LIBADD = $(GST_BASE_LIBS) \
			  $(GST_LIBS)

libgstdvrdrm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdvrdrm_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstdvrdrmdecrypt.h \
	gstdvrdrmencrypt.h
