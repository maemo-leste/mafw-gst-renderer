#
# Makefile.am for MAFW gst renderer library.
#
# Author: Zeeshan Ali <zeeshan.ali@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

plugin_LTLIBRARIES		= mafw-gst-renderer.la

BUILT_SOURCES			= mafw-gst-renderer-marshal.c \
				  mafw-gst-renderer-marshal.h

mafw_gst_renderer_la_SOURCES	= $(BUILT_SOURCES) \
				  blanking.c blanking.h \
				  mafw-gst-renderer.c mafw-gst-renderer.h \
				  mafw-gst-renderer-utils.c mafw-gst-renderer-utils.h \
				  mafw-gst-renderer-worker.c mafw-gst-renderer-worker.h \
				  mafw-gst-renderer-state.c mafw-gst-renderer-state.h \
				  mafw-gst-renderer-state-playing.c mafw-gst-renderer-state-playing.h \
				  mafw-gst-renderer-state-paused.c mafw-gst-renderer-state-paused.h \
				  mafw-gst-renderer-state-stopped.c mafw-gst-renderer-state-stopped.h \
				  mafw-gst-renderer-state-transitioning.c mafw-gst-renderer-state-transitioning.h \
				  mafw-playlist-iterator.c mafw-playlist-iterator.h

mafw_gst_renderer_la_CPPFLAGS	= $(DEPS_CFLAGS) -DPREFIX=\"$(prefix)\" \
				  $(_CFLAGS)
mafw_gst_renderer_la_LDFLAGS	= -avoid-version -module $(_LDFLAGS)
mafw_gst_renderer_la_LIBADD	= $(DEPS_LIBS) -lgstinterfaces-0.10 \
				  -lgstpbutils-0.10

if HAVE_GDKPIXBUF
mafw_gst_renderer_la_SOURCES += gstscreenshot.c gstscreenshot.h
mafw_gst_renderer_la_CPPFLAGS += $(GDKPIXBUF_CFLAGS)
mafw_gst_renderer_la_LIBADD += $(GDKPIXBUF_LIBS)
endif

if HAVE_CONIC
mafw_gst_renderer_la_CPPFLAGS += $(CONIC_CFLAGS)
mafw_gst_renderer_la_LIBADD += $(CONIC_LIBS)
endif

mafw-gst-renderer-marshal.c: mafw-gst-renderer-marshal.list
	( \
		echo '#include "mafw-gst-renderer-marshal.h"'; \
		$(GLIB_GENMARSHAL) --prefix=mafw_gst_renderer_marshal --body $^ \
	) > $@

mafw-gst-renderer-marshal.h: mafw-gst-renderer-marshal.list
	$(GLIB_GENMARSHAL) --prefix=mafw_gst_renderer_marshal --header \
		$^ > $@

EXTRA_DIST			= mafw-gst-renderer-marshal.list
CLEANFILES			= *.gcno *.gcda
MAINTAINERCLEANFILES		= Makefile.in *.loT
