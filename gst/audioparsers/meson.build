audioparsers_src = [
  'gstaacparse.c',
  'gstamrparse.c',
  'gstac3parse.c',
  'gstac4parse.c',
  'gstflacparse.c',
  'gstmpegaudioparse.c',
  'gstsbcparse.c',
  'gstwavpackparse.c',
  'plugin.c',
]

gstaudioparsers = library('gstaudioparsers',
  audioparsers_src,
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gstbase_dep, gstpbutils_dep,
                  gstaudio_dep, gsttag_dep],
  install : true,
  install_dir : plugins_install_dir,
)
