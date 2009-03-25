#
# Makefile.am for MAFW gst renderer library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

TESTS				= check-mafw-gst-renderer
TESTS_ENVIRONMENT		= CK_FORK=yes \
				  TESTS_DIR=@abs_srcdir@

noinst_PROGRAMS			= $(TESTS)

AM_CFLAGS			= $(_CFLAGS)
AM_LDFLAGS			= $(_LDFLAGS)

INCLUDES			= -I$(top_srcdir)/libmafw-gst-renderer \
				  $(DEPS_CFLAGS) \
				  $(DEPS_TESTS_CFLAGS) \
				  $(CHECKMORE_CFLAGS)

LDADD				= $(CHECKMORE_LIBS) \
				  $(DEPS_LIBS) \
				  $(DEPS_TESTS_LIBS) \
				  $(top_builddir)/libmafw-gst-renderer/mafw-gst-renderer.la \
				  -lgstinterfaces-0.10 -lgsttag-0.10

if HAVE_GDKPIXBUF
INCLUDES += $(GDKPIXBUF_CFLAGS)
LDADD += $(GDKPIXBUF_LIBS)
endif

if HAVE_CONIC
INCLUDES += $(CONIC_CFLAGS)
LDADD += $(CONIC_LIBS)
endif

EXTRA_DIST			= media/test.wav media/test.avi media/testframe.png

# -----------------------------------------------
# Test programs build specs
# -----------------------------------------------

check_mafw_gst_renderer_SOURCES	= check-main.c \
				  check-mafw-gst-renderer.c \
				  mafw-mock-playlist.c mafw-mock-playlist.h

CLEANFILES			= $(TESTS) mafw.db *.gcno *.gcda
MAINTAINERCLEANFILES		= Makefile.in

# Run valgrind on tests.
VG_OPTS				:=  --suppressions=test.suppressions --tool=memcheck \
				    --leak-check=full --show-reachable=yes
vg: $(TESTS)
	for p in $^; do \
		G_SLICE=always-malloc G_DEBUG=gc-friendly WAIT_TIMEOUT=25000 \
		libtool --mode=execute valgrind $(VG_OPTS) $$p 2>vglog.$$p; \
	done;
	-rm -f vgcore.*
