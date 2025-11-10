plugin_LTLIBRARIES = libgstdvrmpegtsdemux.la

libgstdvrmpegtsdemux_la_SOURCES = \
	mpegtspacketizer.c \
	mpegtsbase.c	\
	mpegtsparse.c \
	tsdemux.c	\
	gsttsdemux.c \
	pesparse.c

libgstdvrmpegtsdemux_la_CFLAGS = \
	$(GST_PLUGINS_COOL_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-DGstTSDemux=GstDvrTSDemux -DGstTSDemuxClass=GstDvrTSDemuxClass \
	-DMpegTSBase=DvrMpegTSBase -DMpegTSBaseClass=DvrMpegTSBaseClass \
	-DMpegTSPacketizer2=DvrMpegTSPacketizer2 -DMpegTSPacketizer2Class=DvrMpegTSPacketizer2Class \
	-DMpegTSParse2=DvrMpegTSParse2 -DMpegTSParse2Class=DvrMpegTSParse2Class \
	-DMpegTSParsePad=DvrMpegTSParsePad \
	-DGstNalParser=GstDvrNalParser \
	-DHDCPInfo=DvrHDCPInfo \
	-DTSDemuxStream=DvrTSDemuxStream \
	-DTSDemuxH264ParsingInfos=DvrTSDemuxH264ParsingInfos \
	-DTSDemuxJP2KParsingInfos=DvrTSDemuxJP2KParsingInfos \
	-DMpegTSBaseStream=MpegTSBaseStream \
	-DMpegTSBaseProgram=MpegTSBaseProgram
libgstdvrmpegtsdemux_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/dvrmpegts/libgstdvrmpegts-$(GST_API_VERSION).la \
	-lgstcodecparsers-@GST_API_VERSION@ \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-@GST_API_VERSION@ -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstdvrmpegtsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdesc.h   \
	mpegtsbase.h	\
	mpegtspacketizer.h \
	mpegtsparse.h \
	tsdemux.h	\
	pesparse.h
