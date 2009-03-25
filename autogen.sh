#!/bin/sh
# Run this to generate all the initial makefiles, etc.

export AUTOMAKE="automake-1.9"
export ACLOCAL=`echo $AUTOMAKE | sed s/automake/aclocal/`

autoreconf -v -f -i || exit 1
test -n "$NOCONFIGURE" || ./configure \
	--enable-debug --enable-maintainer-mode "$@"
