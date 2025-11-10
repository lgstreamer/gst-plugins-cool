if HAVE_MACHINE
bin_PROGRAMS = shvc-static-play-tv
else
bin_PROGRAMS = decodebin3_decproxy playbin3_decproxy gst-play shvc-static-play
endif

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

decodebin3_decproxy_SOURCES = decodebin3-decproxy.c
decodebin3_decproxy_CFLAGS = $(AM_CFLAGS)
decodebin3_decproxy_LDFLAGS = \
  $(GST_LIBS)

playbin3_decproxy_SOURCES = playbin3-decproxy.c
playbin3_decproxy_CFLAGS = \
  $(AM_CFLAGS) \
  $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la
playbin3_decproxy_LDFLAGS = \
  $(GST_LIBS) \
  $(GST_PLUGINS_COOL_LIBS)

gst_play_SOURCES = gst-play.c gst-play-kb.c gst-play-kb.h
gst_play_CFLAGS = \
  $(AM_CFLAGS) \
  -D_GST_COOL_ENABLE_OPTION_PARSING \
  $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la
gst_play_LDADD = \
  $(GST_BASE_LIBS) \
  -lgstpbutils-@GST_API_VERSION@ \
  -lgsttag-@GST_API_VERSION@ \
  -lgstvideo-@GST_API_VERSION@ \
  -lgstaudio-@GST_API_VERSION@ \
  $(GST_LIBS) \
  $(LIBM) \
  $(GST_PLUGINS_COOL_LIBS)
noinst_HEADERS = gst-play-kb.h

shvc_static_play_SOURCES = shvc-static-play.c gst-play-kb.c gst-play-kb.h
shvc_static_play_CFLAGS = \
  $(AM_CFLAGS) \
  -D_GST_COOL_ENABLE_OPTION_PARSING \
  $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la
shvc_static_play_LDADD = \
  $(GST_BASE_LIBS) \
  -lgstpbutils-@GST_API_VERSION@ \
  -lgsttag-@GST_API_VERSION@ \
  -lgstvideo-@GST_API_VERSION@ \
  -lgstaudio-@GST_API_VERSION@ \
  $(GST_LIBS) \
  $(LIBM) \
  $(GST_PLUGINS_COOL_LIBS)

shvc_static_play_tv_SOURCES = shvc-static-play-tv.c gst-play-kb.c gst-play-kb.h
shvc_static_play_tv_CFLAGS = \
  $(AM_CFLAGS) \
  -D_GST_COOL_ENABLE_OPTION_PARSING \
  $(top_builddir)/gst-libs/gst/cool/libgstcool-@GST_API_VERSION@.la
shvc_static_play_tv_LDADD = \
  $(GST_BASE_LIBS) \
  -lgstpbutils-@GST_API_VERSION@ \
  -lgsttag-@GST_API_VERSION@ \
  -lgstvideo-@GST_API_VERSION@ \
  -lgstaudio-@GST_API_VERSION@ \
  $(GST_LIBS) \
  $(LIBM) \
  $(GST_PLUGINS_COOL_LIBS)
