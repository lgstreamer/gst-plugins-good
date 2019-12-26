mp4_sources = [
  'isomp4-plugin.c',
  'qtdemux.c',
  'qtdemux_types.c',
  'qtdemux_dump.c',
  'qtdemux_lang.c',
  'gstisoff.c',
  'descriptors.c',
  'properties.c',
]

mp4_args = [
    '-DGstQTDemux=GstQTDemux_1_8',
    '-DGstQTDemuxClass=GstQTDemux_1_8Class'
]

gstsubtitle_dep = dependency('gstreamer-subtitle-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'subtitle_dep'])

gstisomp4_1_8 = library('gstisomp4_1_8',
  mp4_sources,
  c_args : gst_plugins_good_args + mp4_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstriff_dep, gstaudio_dep, gstvideo_dep,
                  gstsubtitle_dep, gstpbutils_dep, zlib_dep],
  install : true,
  install_dir : plugins_install_dir,
)
