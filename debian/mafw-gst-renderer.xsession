#!/bin/sh

if test -x /usr/sbin/dsmetool; then
	/usr/bin/mafw.sh start mafw-gst-renderer -7
fi
