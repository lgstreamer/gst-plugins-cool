plugin_LTLIBRARIES = libgstcooltracer.la

# sources used to compile this plug-in
libgstcooltracer_la_SOURCES = \
	gsttraceelement.c \
	gsttracepad.c \
	gsttraceprocstat.c \
	gstcooltracer.c \
	gstpmlog.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstcooltracer_la_CFLAGS = $(GST_CFLAGS)

libgstcooltracer_la_LIBADD = $(GST_BASE_LIBS) \
			  $(GST_LIBS) \
			  $(PMLOG_LIBS)

libgstcooltracer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcooltracer_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gsttracecommon.h \
	gsttraceelement.h \
	gsttracepad.h \
	gsttraceprocstat.h \
	gstpmlog.h \
	gstcooltracer.h
