lib_LTLIBRARIES = libgstdvrmpegts-@GST_API_VERSION@.la

libgstdvrmpegts_@GST_API_VERSION@_la_SOURCES = \
	gstmpegtssection.c \
	gstmpegtsdescriptor.c \
	gst-dvb-descriptor.c \
	gst-dvb-section.c \
	gst-atsc-section.c

libgstdvrmpegts_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/dvrmpegts

noinst_HEADERS = gstmpegts-private.h

libgstdvrmpegts_@GST_API_VERSION@include_HEADERS = \
	gstmpegtssection.h 			\
	gst-atsc-section.h			\
	gst-dvb-section.h			\
	gst-scte-section.h			\
	gstmpegtsdescriptor.h			\
	gst-dvb-descriptor.h			\
	mpegts-prelude.h			\
	mpegts.h

nodist_libgstdvrmpegts_@GST_API_VERSION@include_HEADERS = \
	gstmpegts-enumtypes.h

libgstdvrmpegts_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_COOL_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS) \
	-DGstMpegtsSection=GstDvrMpegtsSection \
	-DGstMpegtsSatelliteDeliverySystemDescriptor=GstDvrMpegtsSatelliteDeliverySystemDescriptor \
	-DGstMpegtsCableDeliverySystemDescriptor=GstMpegtsCableDeliverySystemDescriptor \
	-DGstMpegtsDVBServiceListItem=GstDvrMpegtsDVBServiceListItem \
	-DGstMpegtsDVBLinkageMobileHandOver=GstDvrMpegtsDVBLinkageMobileHandOver \
	-DGstMpegtsDVBLinkageEvent=GstDvrMpegtsDVBLinkageEvent \
	-DGstMpegtsDVBLinkageExtendedEvent=GstDvrMpegtsDVBLinkageExtendedEvent \
	-DGstMpegtsExtendedEventItem=GstDvrMpegtsExtendedEventItem \
	-DGstMpegtsContent=GstDvrMpegtsContent \
	-DGstMpegtsDVBParentalRatingItem=GstDvrMpegtsDVBParentalRatingItem \
	-DGstMpegtsTerrestrialDeliverySystemDescriptor=GstDvrMpegtsTerrestrialDeliverySystemDescriptor \
	-DDGstMpegtsDvbMultilingualNetworkNameItem=GstDvrMpegtsDvbMultilingualNetworkNameItem \
	-DGstMpegtsDvbMultilingualBouquetNameItem=GstDvrMpegtsDvbMultilingualBouquetNameItem \
	-DGstMpegtsDvbMultilingualServiceNameItem=GstDvrMpegtsDvbMultilingualServiceNameItem \
	-DGstMpegtsDvbMultilingualComponentItem=GstDvrMpegtsDvbMultilingualComponentItem \
	-DGstMpegtsT2DeliverySystemCellExtension=GstDvrMpegtsT2DeliverySystemCellExtension \
	-DGstMpegtsT2DeliverySystemCell=GstDvrMpegtsT2DeliverySystemCell \
	-DGstMpegtsLogicalChannelDescriptor=GstDvrMpegtsLogicalChannelDescriptor \
	-DGstMpegtsLogicalChannel=GstDvrMpegtsLogicalChannel

libgstdvrmpegts_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgstdvrmpegts_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

glib_enum_headers=gstmpegtssection.h	\
	gstmpegtsdescriptor.h		\
	gst-atsc-section.h		\
	gst-dvb-section.h		\
	gst-scte-section.h		\
	gst-dvb-descriptor.h

glib_enum_define=GST_MPEGTS
glib_gen_prefix=gst_mpegts
glib_gen_basename=gstmpegts
glib_gen_decl_banner=GST_MPEGTS_API
glib_gen_decl_include=\#include <gst/dvrmpegts/mpegts-prelude.h>

BUILT_SOURCES = gstmpegts-enumtypes.c gstmpegts-enumtypes.h
nodist_libgstdvrmpegts_@GST_API_VERSION@_la_SOURCES = gstmpegts-enumtypes.c

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak
