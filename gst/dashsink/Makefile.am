plugin_LTLIBRARIES = libgstdashsink.la

# sources used to compile this plug-in
libgstdashsink_la_SOURCES = \
	gstdashsink.cpp \
	xmlelement.cpp \
	manifestgenerator.cpp \
	plugin.cpp

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdashsink_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(LIBXML2_CFLAGS)
libgstdashsink_la_CXXFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(LIBXML2_CFLAGS)
libgstdashsink_la_LIBADD = \
  $(GST_PLUGINS_BASE_LIBS) \
  -lgsttag-@GST_API_VERSION@ \
  $(GST_LIBS) $(LIBXML2_LIBS)
libgstdashsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdashsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstdashsink.hpp \
	manifestgenerator.hpp \
	xmlelement.hpp
