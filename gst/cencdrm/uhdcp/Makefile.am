plugin_LTLIBRARIES = libgstcencdrmuhdcp.la

# sources used to compile this plug-in
libgstcencdrmuhdcp_la_SOURCES = \
	gstcencdrmuhdcp.c \
	plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstcencdrmuhdcp_la_CFLAGS = $(GST_CFLAGS)

libgstcencdrmuhdcp_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_PLUGINS_BAD_LIBS) \
	-lgstbasedrm-$(GST_API_VERSION)

libgstcencdrmuhdcp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstcencdrmuhdcp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstcencdrmuhdcp.h
