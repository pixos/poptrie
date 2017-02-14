#
# Copyright (c) 2015-2017 Hirochika Asai <asai@jar.jp>
# All rights reserved.
#

EXTRA_DIST = README.md LICENSE tests/linx-rib.20141217.0000-p46.txt tests/linx-rib-ipv6.20141225.0000.p69.txt tests/linx-rib.20141217.0000-p52.txt tests/linx-update.20141217.0000-p52.txt

noinst_HEADERS = buddy.h

bin_PROGRAMS = poptrie_test_basic poptrie_test_basic6
lib_LTLIBRARIES = libpoptrie.la
libpoptrie_la_SOURCES = poptrie.c poptrie4.c poptrie6.c poptrie.h buddy.c buddy.h poptrie_private.h

poptrie_test_basic_SOURCES = tests/basic.c
poptrie_test_basic_LDADD = libpoptrie.la
poptrie_test_basic_DEPENDENCIES = libpoptrie.la

poptrie_test_basic6_SOURCES = tests/basic6.c
poptrie_test_basic6_LDADD = libpoptrie.la
poptrie_test_basic6_DEPENDENCIES = libpoptrie.la

CLEANFILES = *~

test: all
	@echo "Testing all..."
	$(top_builddir)/poptrie_test_basic
	$(top_builddir)/poptrie_test_basic6

