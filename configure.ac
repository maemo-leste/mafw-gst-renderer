#
# configure.ac for MAFW gstreamer renderer library
# 
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

AC_PREREQ([2.53])
AC_INIT([mafw-gst-renderer], [0.1.2009.23-3])

AC_CONFIG_SRCDIR([libmafw-gst-renderer/mafw-gst-renderer.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign tar-ustar])
AM_MAINTAINER_MODE

AC_DISABLE_STATIC

dnl Prevent AC_PROG_CC adding '-g -O2' to CFLAGS.
SAVEDCFLAGS="$CFLAGS"
AC_PROG_CC
if test "x$GCC" = xyes; then
	CFLAGS="$SAVEDCFLAGS"
fi

AC_PROG_LIBTOOL
AC_PROG_INSTALL

# DISABLED_BY_DEFAULT(NAME, DESCRIPTION)
# ---------------------------------
# Creates a new --enable-* option, with default value `no'.
AC_DEFUN([DISABLED_BY_DEFAULT], [dnl
         AC_ARG_ENABLE([$1], AS_HELP_STRING([--enable-$1], [$2]), [],[dnl
         m4_bpatsubst([enable_$1], [[^0-9a-z]], [_])=no])dnl
])# DISABLED_BY_DEFAULT

# ENABLED_BY_DEFAULT(NAME, DESCRIPTION)
# ---------------------------------
# Creates a new --disable-* option, with default value `yes'.
AC_DEFUN([ENABLED_BY_DEFAULT], [dnl
         AC_ARG_ENABLE([$1], AS_HELP_STRING([--disable-$1], [$2]), [],[dnl
         m4_bpatsubst([enable_$1], [[^0-9a-z]], [_])=yes])dnl
])# ENABLED_BY_DEFAULT

dnl Prerequisites.

GSTREAMER_VERSION=0.10.20

AM_PATH_GLIB_2_0(2.15.0, [], [], [glib])
PKG_CHECK_MODULES(DEPS,
		  gobject-2.0 >= 2.0
		  gstreamer-0.10 >= $GSTREAMER_VERSION
		  gstreamer-plugins-base-0.10 >= $GSTREAMER_VERSION
		  mafw >= 0.1
		  libosso >= 2.0
		  x11
		  hal
		  totem-plparser
                  libpulse-mainloop-glib >= 0.9.15
)

dnl Check for GdkPixbuf, needed for dumping current frame
GDKPIXBUF_REQUIRED=2.12.0
AC_ARG_ENABLE(gdkpixbuf,
	      AS_HELP_STRING([--disable-gdkpixbuf],
			     [Disable GdkPixbuf support, required for current frame dumping]),,
	      [enable_gdkpixbuf=auto])

if test "x$enable_gdkpixbuf" != "xno"; then
   PKG_CHECK_MODULES(GDKPIXBUF,
		     [gdk-pixbuf-2.0 >= $GDKPIXBUF_REQUIRED],
		     [have_gdkpixbuf=yes],
		     [have_gdkpixbuf=no])
   AC_SUBST(GDKPIXBUF_LIBS)
   AC_SUBST(GDKPIXBUF_CFLAGS)

   if test "x$have_gdkpixbuf" = "xyes"; then
      AC_DEFINE(HAVE_GDKPIXBUF, [], [Define if we have GdkPixbuf])
   fi
else
   have_gdkpixbuf="no (disabled)"
fi

if test "x$enable_gdkpixbuf" = "xyes"; then
   if test "x$have_gdkpixbuf" != "xyes"; then
      AC_MSG_ERROR([Couldn't find GdkPixbuf >= $GDKPIXBUF_REQUIRED.])
   fi
fi

AM_CONDITIONAL(HAVE_GDKPIXBUF, test "x$have_gdkpixbuf" = "xyes")


dnl Check for Conic, needed connection error handling
CONIC_REQUIRED=0.16
AC_ARG_ENABLE(conic,
	      AS_HELP_STRING([--disable-conic],
			     [Disable Conic support, required to handle network errors]),,
	      [enable_conic=auto])

if test "x$enable_conic" != "xno"; then
   PKG_CHECK_MODULES(CONIC,
		     [conic >= $CONIC_REQUIRED],
		     [have_conic=yes],
		     [have_conic=no])
   AC_SUBST(CONIC_LIBS)
   AC_SUBST(CONIC_CFLAGS)

   if test "x$have_conic" = "xyes"; then
      AC_DEFINE(HAVE_CONIC, [], [Define if we have Conic])
   fi
else
   have_conic="no (disabled)"
fi

if test "x$enable_conic" = "xyes"; then
   if test "x$have_conic" != "xyes"; then
      AC_MSG_ERROR([Couldn't find Conic >= $CONIC_REQUIRED.])
   fi
fi

AM_CONDITIONAL(HAVE_CONIC, test "x$have_conic" = "xyes")



plugindir=`$PKG_CONFIG --variable=plugindir mafw`
AC_SUBST(plugindir)

dnl Default compile flags.  (NOTE: CFLAGS is reserved for the user!)

AC_SUBST([_CFLAGS])
AC_SUBST([_LDFLAGS])
_CFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations"
_CFLAGS="$_CFLAGS -ggdb3"

dnl Configure-time options.

dnl Debugging.
DISABLED_BY_DEFAULT([debug], [compile with debug flags and extra output])
if test "x$enable_debug" = xyes; then
	AC_DEFINE([MAFW_DEBUG], [1], [Enable debugging related parts.])
	_CFLAGS="$_CFLAGS -O0 -Werror -DGTK_DISABLE_DEPRECATED"
else
	AC_DEFINE([G_DEBUG_DISABLE], [1], [Disable g_debug() calls.])
	_CFLAGS="$_CFLAGS -O2"
fi
AS_IF([test "x$enable_debug" = xyes],
	[AC_DEFINE([MAFW_DEBUG], [1], [Enable extra debug messages])
	 CFLAGS="$CFLAGS -Werror -O0 -ggdb3 -DGTK_DISABLE_DEPRECATED"],
	[AC_DEFINE([G_DEBUG_DISABLE], [1], [Disable g_debug calls])
	 CFLAGS="$CFLAGS -O2"])


dnl Tests.
DISABLED_BY_DEFAULT([tests], [disable unit tests])
if test "x${SBOX_DPKG_INST_ARCH}" = "xarmel"; then
   AC_MSG_WARN([Tests are disabled for compilation in armel])
   enable_tests="no"
fi
if test "x$enable_tests" = xyes; then
	PKG_CHECK_MODULES(CHECKMORE, [checkmore, check >= 0.9.4])
	if test -z "$CHECKMORE_LIBS"; then
		AC_MSG_WARN([checkmore is needed for unit tests!])
	fi
fi
AM_CONDITIONAL(ENABLE_TESTS,
               [test "x$enable_tests" = xyes && test -n "$CHECKMORE_LIBS"])

dnl Mute
DISABLED_BY_DEFAULT([mute], [enable mute handling])
if test "x$enable_mute" = xyes; then
	AC_DEFINE([MAFW_GST_RENDERER_ENABLE_MUTE], [1], [Enable mute.])
fi

dnl Tracing.
DISABLED_BY_DEFAULT([tracing], [enable function instrumentation (tracing)])
if test "x$enable_tracing" = xyes; then
	_CFLAGS="$_CFLAGS -finstrument-functions -rdynamic"
fi

dnl Coverage.
DISABLED_BY_DEFAULT([coverage], [enable coverage data generation (gcov)])
if test "x$enable_coverage" = xyes; then
	AC_PATH_PROG(LCOV, [lcov], [lcov])
	if test "x$LCOV" = x; then
		echo You need to install lcov to get actual reports!
		echo See http://ltp.sf.net/coverage/lcov.php
	fi
	if test "x$SBOX_USE_CCACHE" == xyes; then
		AC_MSG_ERROR([Please set SBOX_USE_CCACHE=no to use coverage.])
	fi
	_CFLAGS="$_CFLAGS -fprofile-arcs -ftest-coverage"
	_LDFLAGS="$_LDFLAGS -g -lgcov"
fi
AM_CONDITIONAL(ENABLE_COVERAGE,
               [test "x$enable_coverage" != xno && test -n "$LCOV"])

dnl Output files.

AC_CONFIG_FILES([
  Makefile
  mafw-gst-renderer-uninstalled.pc
  libmafw-gst-renderer/Makefile
  tests/Makefile
  debian/mafw-gst-renderer.install
])

AC_OUTPUT
