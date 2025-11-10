dvrtsdemux_sources = [
  'mpegtspacketizer.c',
  'mpegtsbase.c',
  'mpegtsparse.c',
  'tsdemux.c',
  'gsttsdemux.c',
  'pesparse.c'
]

dvrtsdemux_deps = [
  gstdvrmpegts_dep,
  gstcodecparsers_dep,
  gsttag_dep,
  gstpbutils_dep,
  gstaudio_dep,
  gstbase_dep,
  gst_dep,
  libm,
  libdl
]

dvrtsdemux_args = [
  '-DGstTSDemux=GstDvrTSDemux',
  '-DGstTSDemuxClass=GstDvrTSDemuxClass',
  '-DMpegTSBase=DvrMpegTSBase',
  '-DMpegTSBaseClass=DvrMpegTSBaseClass',
  '-DMpegTSPacketizer2=DvrMpegTSPacketizer2',
  '-DMpegTSPacketizer2Class=DvrMpegTSPacketizer2Class',
  '-DMpegTSParse2=DvrMpegTSParse2',
  '-DMpegTSParse2Class=DvrMpegTSParse2Class',
  '-DMpegTSParsePad=DvrMpegTSParsePad',
  '-DGstNalParser=GstDvrNalParser',
  '-DHDCPInfo=DvrHDCPInfo',
  '-DTSDemuxStream=DvrTSDemuxStream',
  '-DTSDemuxH264ParsingInfos=DvrTSDemuxH264ParsingInfos',
  '-DTSDemuxJP2KParsingInfos=DvrTSDemuxJP2KParsingInfos',
  '-DMpegTSBaseStream=MpegTSBaseStream',
  '-DMpegTSBaseProgram=MpegTSBaseProgram'
]

gstdvrmpegtsdemux = library('gstdvrmpegtsdemux',
  dvrtsdemux_sources,
  c_args : gst_plugins_cool_args + dvrtsdemux_args,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  install_dir : plugins_install_dir,
  dependencies : dvrtsdemux_deps,
)
