project('gst-plugins-good', 'c',
  version : '1.14.4',
  meson_version : '>= 0.40.1',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0].to_int()
gst_version_minor = version_arr[1].to_int()
gst_version_micro = version_arr[2].to_int()
 if version_arr.length() == 4
  gst_version_nano = version_arr[3].to_int()
else
  gst_version_nano = 0
endif

have_cxx = add_languages('cpp', required : false)

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cc = meson.get_compiler('c')

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things gstreamer does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious
  add_project_arguments(
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      language : 'c')
  # Disable SAFESEH with MSVC for plugins and libs that use external deps that
  # are built with MinGW
  noseh_link_args = ['/SAFESEH:NO']
else
  noseh_link_args = []
endif

# Symbol visibility
if cc.has_argument('-fvisibility=hidden')
  add_project_arguments('-fvisibility=hidden', language: 'c')
endif

# Disable strict aliasing
if cc.has_argument('-fno-strict-aliasing')
  add_project_arguments('-fno-strict-aliasing', language: 'c')
endif

cdata = configuration_data()

check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_FCNTL_H', 'fcntl.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_PROCESS_H', 'process.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_IOCTL_H', 'sys/ioctl.h'],
  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
  ['HAVE_SYS_SOCKET_H', 'sys/socket.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TIME_H', 'sys/time.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_V4L2_EXT_H', 'linux/v4l2-ext/videodev2-ext.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_functions = [
  ['HAVE_ASINH', 'asinh', '#include<math.h>'],
  ['HAVE_CLOCK_GETTIME', 'clock_gettime', '#include<time.h>'],
  ['HAVE_COSH', 'cosh', '#include<math.h>'],
# check token HAVE_CPU_ALPHA
# check token HAVE_CPU_ARM
# check token HAVE_CPU_CRIS
# check token HAVE_CPU_CRISV32
# check token HAVE_CPU_HPPA
# check token HAVE_CPU_I386
# check token HAVE_CPU_IA64
# check token HAVE_CPU_M68K
# check token HAVE_CPU_MIPS
# check token HAVE_CPU_PPC
# check token HAVE_CPU_PPC64
# check token HAVE_CPU_S390
# check token HAVE_CPU_SPARC
# check token HAVE_CPU_X86_64
  ['HAVE_DCGETTEXT', 'dcgettext', '#include<libintl.h>'],
# check token HAVE_DIRECTSOUND
# check token HAVE_EXPERIMENTAL
# check token HAVE_EXTERNAL
# check token HAVE_FPCLASS
# check token HAVE_GCC_ASM
  ['HAVE_GETPAGESIZE', 'getpagesize', '#include<unistd.h>'],
# check token HAVE_GETTEXT
# check token HAVE_GST_V4L2
# check token HAVE_IOS
  ['HAVE_ISINF', 'isinf', '#include<math.h>'],
# check token HAVE_LIBV4L2
  ['HAVE_MMAP', 'mmap', '#include<sys/mman.h>'],
  ['HAVE_MMAP64', 'mmap64', '#include<sys/mman.h>'],
# check token HAVE_OSX_AUDIO
# check token HAVE_OSX_VIDEO
# check token HAVE_RDTSC
  ['HAVE_SINH', 'sinh', '#include<math.h>'],
# check token HAVE_WAVEFORM
]

libm = cc.find_library('m', required : false)

foreach f : check_functions
  if cc.has_function(f.get(1), prefix : f.get(2), dependencies : libm)
    cdata.set(f.get(0), 1)
  endif
endforeach

cdata.set('SIZEOF_CHAR', cc.sizeof('char'))
cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))
cdata.set('SIZEOF_VOIDP', cc.sizeof('void*'))
cdata.set('SIZEOF_OFF_T', cc.sizeof('off_t'))

# Here be fixmes.
# FIXME: check if this is correct
cdata.set('HAVE_CPU_X86_64', host_machine.cpu() == 'amd64')
cdata.set('HAVE_GCC_ASM', cc.get_id() != 'msvc')
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('GST_LICENSE', '"LGPL"')
cdata.set('PACKAGE', '"gst-plugins-good"')
cdata.set('GETTEXT_PACKAGE', '"gst-plugins-good-1.0"')

warning_flags = [
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wwrite-strings',
  '-Winit-self',
  '-Wmissing-include-dirs',
  '-Wno-multichar',
  '-Wvla',
  '-Wpointer-arith',
]

warning_c_flags = [
  '-Wmissing-prototypes',
  '-Wdeclaration-after-statement',
  '-Wold-style-definition',
  '-Waggregate-return',
]

if have_cxx
  cxx = meson.get_compiler('cpp')
endif

foreach extra_arg : warning_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
  if have_cxx and cxx.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'cpp')
  endif
endforeach

foreach extra_arg : warning_c_flags
  if cc.has_argument (extra_arg)
    add_project_arguments([extra_arg], language: 'c')
  endif
endforeach

# GStreamer package name and origin url
gst_package_name = get_option('with-package-name')
if gst_package_name == ''
  if gst_version_nano == 0
    gst_package_name = 'GStreamer Good Plug-ins source release'
  elif gst_version_nano == 1
    gst_package_name = 'GStreamer Good Plug-ins git'
  else
    gst_package_name = 'GStreamer Good Plug-ins prerelease'
  endif
endif
cdata.set_quoted('GST_PACKAGE_NAME', gst_package_name)
cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('with-package-origin'))

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
if host_machine.system() != 'windows'
  gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
    fallback : ['gstreamer', 'gst_check_dep'])
endif
gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_controller_dep'])

gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'allocators_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstfft_dep = dependency('gstreamer-fft-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'fft_dep'])
gstriff_dep = dependency('gstreamer-riff-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'riff_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtsp_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])

# GStreamer OpenGL
gstgl_dep = dependency('gstreamer-gl-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'gstgl_dep'], required: false)

build_gstgl = gstgl_dep.found() # FIXME: add option?

if build_gstgl
  if gstgl_dep.type_name() == 'pkgconfig'
    gst_gl_apis = gstgl_dep.get_pkgconfig_variable('gl_apis').split()
    gst_gl_winsys = gstgl_dep.get_pkgconfig_variable('gl_winsys').split()
    gst_gl_platforms = gstgl_dep.get_pkgconfig_variable('gl_platforms').split()
  else
    gstbase = subproject('gst-plugins-base')
    gst_gl_apis = gstbase.get_variable('enabled_gl_apis')
    gst_gl_winsys = gstbase.get_variable('enabled_gl_winsys')
    gst_gl_platforms = gstbase.get_variable('enabled_gl_platforms')
  endif

  message('GStreamer OpenGL window systems: @0@'.format(' '.join(gst_gl_winsys)))
  message('GStreamer OpenGL platforms: @0@'.format(' '.join(gst_gl_platforms)))
  message('GStreamer OpenGL apis: @0@'.format(' '.join(gst_gl_apis)))

  foreach ws : ['x11', 'wayland', 'android', 'cocoa', 'eagl', 'win32', 'dispmanx', 'viv_fb']
    set_variable('gst_gl_have_window_@0@'.format(ws), gst_gl_winsys.contains(ws))
  endforeach

  foreach p : ['glx', 'egl', 'cgl', 'eagl', 'wgl']
    set_variable('gst_gl_have_platform_@0@'.format(p), gst_gl_platforms.contains(p))
  endforeach

  foreach api : ['opengl', 'gles2']
    set_variable('gst_gl_have_api_@0@'.format(api), gst_gl_apis.contains(api))
  endforeach
endif

zlib_dep = dependency('zlib', required : false)
bz2lib = cc.find_library('bz2', required : false)
glib_deps = [dependency('glib-2.0', version : glib_req, fallback: ['glib', 'libglib_dep']),
             dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])]
gio_dep = dependency('gio-2.0', fallback: ['glib', 'libgio_dep'])

cdata.set('HAVE_ZLIB', zlib_dep.found())
cdata.set('HAVE_BZ2', bz2lib.found())

# Check all of the things.
# TODO: None of these are actually used yet because
# the build files haven't been written
deps = [
  ['gtk_dep','gtk+-3.0', '', ''],
  ['gtkx_dep','gtk+-x11-3.0', '', ''],
  ['caca_dep','caca', '', ''],
  ['libraw1394_dep','libraw1394', '>=2.0.0', ''],
  ['libiec61883_dep','libiec61883', '>=1.0.0', ''],
]

foreach d : deps
  varname = d[0]
  depname = d[1]
  version = d[2]
  confhname = d[3]
  if version == ''
    curdep = dependency(depname, required : false)
  else
    curdep = dependency(depname, required : false, version : version)
  endif
  set_variable(varname, curdep)
  if curdep.found() and confhname != ''
    cdata.set(confhname, 1)
  endif
endforeach

gst_plugins_good_args = ['-DHAVE_CONFIG_H']
configinc = include_directories('.')
libsinc = include_directories('gst-libs')

have_orcc = false
orcc_args = []
if get_option('use_orc') != 'no'
  need_orc = get_option('use_orc') == 'yes'
  # Used by various libraries/elements that use Orc code
  orc_dep = dependency('orc-0.4', version : '>= 0.4.17', required : need_orc)
  orcc = find_program('orcc', required : need_orc)
  if orc_dep.found() and orcc.found()
    have_orcc = true
    orcc_args = [orcc, '--include', 'glib.h']
    cdata.set('HAVE_ORC', 1)
  else
    message('Orc Compiler not found, will use backup C code')
    cdata.set('DISABLE_ORC', 1)
  endif
else
  cdata.set('DISABLE_ORC', 1)
endif

if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')

    if gst_proj.get_variable('disable_gst_debug')
        message('GStreamer debug system is disabled')
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
else
    # We can't check that in the case of subprojects as we won't
    # be able to build against an internal dependency (which is not built yet)
    if not cc.compiles('''
#include <gst/gstconfig.h>
#ifdef GST_DISABLE_GST_DEBUG
#error "debugging disabled, make compiler fail"
#endif''' , dependencies: gst_dep)
        message('GStreamer debug system is disabled')
        add_project_arguments('-Wno-unused', language: 'c')
    else
        message('GStreamer debug system is enabled')
    endif
endif

presetdir = join_paths(get_option('datadir'), 'gstreamer-' + api_version, 'presets')

subdir('gst')
subdir('sys')
subdir('ext')
subdir('tests')
subdir('pkgconfig')

# xgettext is optional (on Windows for instance)
if find_program('xgettext', required : false).found()
  subdir('po')
endif

configure_file(output : 'config.h', configuration : cdata)

python3 = import('python3').find_python()
run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
