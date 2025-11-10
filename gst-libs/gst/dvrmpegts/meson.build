dvrmpegts_sources = [
  'gst-atsc-section.c',
  'gst-dvb-descriptor.c',
  'gst-dvb-section.c',
  'gstmpegtsdescriptor.c',
  'gstmpegtssection.c'
]

dvrmpegts_headers = [
  'gst-atsc-section.h',
  'gst-dvb-descriptor.h',
  'gst-dvb-section.h',
  'gstmpegtsdescriptor.h',
  'gstmpegtssection.h',
  'mpegts-prelude.h',
  'mpegts.h'
]

dvrmpegts_deps = [
  gstbase_dep,
  gst_dep
]

install_headers(dvrmpegts_headers, subdir : 'gstreamer-1.0/gst/dvrmpegts/')

args = [
  '-DGST_USE_UNSTABLE_API',
  '-DGstMpegtsSection=GstDvrMpegtsSection',
  '-DGstMpegtsSatelliteDeliverySystemDescriptor=GstDvrMpegtsSatelliteDeliverySystemDescriptor',
  '-DGstMpegtsCableDeliverySystemDescriptor=GstMpegtsCableDeliverySystemDescriptor',
  '-DGstMpegtsDVBServiceListItem=GstDvrMpegtsDVBServiceListItem',
  '-DGstMpegtsDVBLinkageMobileHandOver=GstDvrMpegtsDVBLinkageMobileHandOver',
  '-DGstMpegtsDVBLinkageEvent=GstDvrMpegtsDVBLinkageEvent',
  '-DGstMpegtsDVBLinkageExtendedEvent=GstDvrMpegtsDVBLinkageExtendedEvent',
  '-DGstMpegtsExtendedEventItem=GstDvrMpegtsExtendedEventItem',
  '-DGstMpegtsContent=GstDvrMpegtsContent',
  '-DGstMpegtsDVBParentalRatingItem=GstDvrMpegtsDVBParentalRatingItem',
  '-DGstMpegtsTerrestrialDeliverySystemDescriptor=GstDvrMpegtsTerrestrialDeliverySystemDescriptor',
  '-DDGstMpegtsDvbMultilingualNetworkNameItem=GstDvrMpegtsDvbMultilingualNetworkNameItem',
  '-DGstMpegtsDvbMultilingualBouquetNameItem=GstDvrMpegtsDvbMultilingualBouquetNameItem',
  '-DGstMpegtsDvbMultilingualServiceNameItem=GstDvrMpegtsDvbMultilingualServiceNameItem',
  '-DGstMpegtsDvbMultilingualComponentItem=GstDvrMpegtsDvbMultilingualComponentItem',
  '-DGstMpegtsT2DeliverySystemCellExtension=GstDvrMpegtsT2DeliverySystemCellExtension',
  '-DGstMpegtsT2DeliverySystemCell=GstDvrMpegtsT2DeliverySystemCell',
  '-DGstMpegtsLogicalChannelDescriptor=GstDvrMpegtsLogicalChannelDescriptor',
  '-DGstMpegtsLogicalChannel=GstDvrMpegtsLogicalChannel'
]

mpegts_mkenum_headers = [
  'gstmpegtsdescriptor.h',
  'gst-atsc-section.h',
  'gst-scte-section.h',
  'gst-dvb-descriptor.h',
  'gst-dvb-section.h'
]

mkenums = find_program('mpegts_mkenum.py')
mpegtsenum_h = custom_target('gstmpegtsenum_h',
  output : 'gstmpegts-enumtypes.h',
  input : mpegts_mkenum_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/dvrmpegts/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

mpegtsenum_c = custom_target('gstmpegtsenum_c',
  output : 'gstmpegts-enumtypes.c',
  input : mpegts_mkenum_headers,
  depends : [mpegtsenum_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstdvrmpegts = library('gstdvrmpegts-' + api_version,
  dvrmpegts_sources, mpegtsenum_c, mpegtsenum_h,
  c_args : gst_plugins_cool_args + args,
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : dvrmpegts_deps,
)

gstdvrmpegts_dep = declare_dependency(link_with : gstdvrmpegts,
  include_directories : [libsinc],
  dependencies : dvrmpegts_deps,
  sources : mpegtsenum_h)
