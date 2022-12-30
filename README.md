LG webOS 5.x GStreamer - Good Plugins
=====================================

## Description

This directory contains the gst-plugins-good source, as compiled by LG to be
included in webOS 5.x devices, such as **LG CX OLED TVs**.

Thus, unless LG applied some changes that have not yet been published, the
binaries produced by compiling the source from this repository should work
as a drop-in replacement for the GStreamer binaries that are officially used
by LG in their CX OLED Smart TVs.

This can be very useful if you have [rooted your TV](https://github.com/RootMyTV/RootMyTV.github.io/issues/85#issuecomment-1295058979)
and want to alter this source to enable or restore functionality that is
not provided by default on your CX model.

For instance, in this specific repository, we have also [**restored** DTS demuxing](https://github.com/lgstreamer/gst-plugins-good/commit/da51deb94e0872a3aac0df1fb775ac0cc435684a)
from the Mastroska plugin, which LG's forcibly removed, on CX models, so that,
provided that you also have a DTS audio decoder plugin, `.mkv` files with a
DTS track can be played natively on your TV. For more on this, please see
[this thread](https://github.com/RootMyTV/RootMyTV.github.io/issues/72#issuecomment-1343204028)
from the [RootMyTV](https://github.com/RootMyTV/RootMyTV.github.io) issue
tracker.

## Origin

This source, which is licensed under LPGL v2.0, was obtained through a legal
inquiry at https://opensource.lge.com/inquiry and was extracted from the
`webOS 5.0 JO 2.0` archive that can be downloaded [here](http://opensource.lge.com/product/list?page=&ctgr=005&subCtgr=006&keyword=OLED65CX5LB).

The changes that have been applied by LG on top of the official GStreamer
1.14.4 source can be found in [this commit](https://github.com/lgstreamer/gst-plugins-good/commit/f0501885dbee72c3dcc49d2f23d92a6fd02af508).

## Compilation

### Toolchain installation

You will need a recent Linux system, with some GTK related system updates as
well as the webosbrew toolchain from https://www.webosbrew.org. On Debian 11,
the toolchain can be installed as follows:

```
apt install cmake doxygen libglib2.0-dev-bin gobject-introspection libgirepository1.0-dev
wget https://github.com/webosbrew/meta-lg-webos-ndk/releases/download/1.0.g-rev.5/webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
chmod 755 webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
./webos-sdk-x86_64-armv7a-neon-toolchain-1.0.g.sh
```

Note that, if using the toolchain above, you should also have compiled and
installed the GStreamer software from https://github.com/lgstreamer/gstreamer
as well as the base plugins from https://github.com/lgstreamer/gst-plugins-base
otherwise the process will complain that your version of GStreamer is too old.

### Build process

Once the toolchain and GStreamer dependencies have been installed, you can
compile these plugins by issuing:

```
git clone https://github.com/lgstreamer/gst-plugins-good.git
cd gst-plugins-good
. /opt/webos-sdk-x86_64/1.0.g/environment-setup-armv7a-neon-webos-linux-gnueabi
./autogen.sh --noconfigure
patch -p1 < gst-plugins-good-1.14.4-make43.patch
./autogen.sh --host=arm-webos-linux-gnueabi --with-sysroot=${SDKTARGETSYSROOT} \
  --prefix=${SDKTARGETSYSROOT}/usr/ \
  --disable-silent-rules --disable-dependency-tracking --disable-gtk-doc \
  --disable-introspection --disable-examples --enable-oss \
  --disable-aalib --disable-aalibtest --disable-directsound \
  --disable-libcaca --disable-libdv --disable-oss4 \
  --disable-osx_audio --disable-osx_video --disable-qt \
  --disable-shout2 --disable-twolame --disable-waveform \
  --enable-fatal-warnings -disable-autodetect --disable-static \
  --enable-bz2 --disable-cairo --disable-debug --disable-dv1394 \
  --enable-flac --disable-gdk_pixbuf --disable-gtk3 \
  --without-gudev --disable-jack --disable-jpeg --disable-lame \
  --disable-law --disable-libpng --without-libv4l2 \
  --disable-mpg123 --disable-orc --enable-pulse --enable-soup \
  --enable-speex --enable-taglib --enable-gst_v4l2 \
  --enable-v4l2-probe --disable-valgrind --disable-vpx \
  --enable-wavpack --disable-x --disable-y4m --enable-zlib \
  --enable-nls --enable-law
./fix_sysroot.sh
make -j6
```
