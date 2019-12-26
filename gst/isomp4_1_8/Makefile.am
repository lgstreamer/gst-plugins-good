
plugin_LTLIBRARIES = libgstisomp4_1_8.la

libgstisomp4_1_8_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
    -DGstQTDemux=GstQTDemux_1_8 -DGstQTDemuxClass=GstQTDemux_1_8Class
libgstisomp4_1_8_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgstriff-@GST_API_VERSION@ \
    -lgstaudio-@GST_API_VERSION@ \
    -lgstvideo-@GST_API_VERSION@ \
    -lgstsubtitle-@GST_API_VERSION@ \
    -lgstrtp-@GST_API_VERSION@ \
    -lgsttag-@GST_API_VERSION@ \
    -lgstpbutils-@GST_API_VERSION@ \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgstisomp4_1_8_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstisomp4_1_8_la_SOURCES = isomp4-plugin.c \
	qtdemux.c qtdemux_types.c qtdemux_dump.c qtdemux_lang.c \
	descriptors.c properties.c gstisoff.c
libgstisomp4_1_8_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	qtatomparser.h   \
	qtdemux.h        \
	qtdemux_types.h  \
	qtdemux_dump.h   \
	qtdemux_lang.h   \
	qtpalette.h      \
	descriptors.h    \
	properties.h     \
	fourcc.h         \
	gstisoff.h
