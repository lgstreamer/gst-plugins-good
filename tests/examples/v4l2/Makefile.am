noinst_PROGRAMS = camctrl v4l2src-renegotiate v4l2scalersrc-negotiate-dmabuf

camctrl_SOURCES = camctrl.c
camctrl_CFLAGS  = $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
camctrl_LDADD = $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) $(GST_LIBS)

v4l2src_renegotiate_SOURCES = v4l2src-renegotiate.c
v4l2src_renegotiate_CFLAGS  = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
v4l2src_renegotiate_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)

v4l2scalersrc_negotiate_dmabuf_SOURCES = v4l2scalersrc-negotiate-dmabuf.c
v4l2scalersrc_negotiate_dmabuf_CFLAGS  = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
v4l2scalersrc_negotiate_dmabuf_LDADD = $(GST_BASE_LIBS) $(GST_LIBS)
