#
# Makefile.am for MAFW gst renderer library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.


SUBDIRS				= libmafw-gst-renderer

if ENABLE_TESTS
SUBDIRS				+= tests
endif

noinst_DATA			= mafw-gst-renderer-uninstalled.pc
EXTRA_DIST			= mafw-gst-renderer-uninstalled.pc.in

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES 		= aclocal.m4 compile config.guess \
				  config.sub configure depcomp \
				  install-sh ltmain.sh Makefile.in \
				  missing config.h.in *-stamp omf.make

if ENABLE_COVERAGE
LCOV_DATA_DIR			= lcov-data
LCOV_DATA_FILE			= lcov.info

distclean-local:
	-rm -rf $(LCOV_DATA_DIR)

lcov-zero-counters:
	$(LCOV) -z -d .

lcov:
	-mkdir -p $(LCOV_DATA_DIR)
	$(LCOV) -c -d . -o $(LCOV_DATA_DIR)/$(LCOV_DATA_FILE)
	genhtml -s $(LCOV_DATA_DIR)/$(LCOV_DATA_FILE) -o $(LCOV_DATA_DIR)
	@echo
	@echo "Please, have a look on ./$(LCOV_DATA_DIR)/index.html for coverage statistics"
	@echo
endif
