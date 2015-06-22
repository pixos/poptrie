#
# Copyright (c) 2015 Hirochika Asai <asai@jar.jp>
# All rights reserved.
#

#SUBDIRS = 
#EXTRA_DIST = 
#noinst_HEADERS = 
#INCLUDES = -I$(top_srcdir)/include

bin_PROGRAMS = poptrie_test_basic
lib_LTLIBRARIES = libpoptrie.la
libpoptrie_la_SOURCES = poptrie.c poptrie.h
poptrie_test_basic_SOURCES = tests/basic.c
poptrie_test_basic_LDADD = libpoptrie.la
#prog_LDFLAGS = -static $(top_builddir)/libpoptrie.la

CLEANFILES = *~

test: all
	@echo "Testing all..."
	$(top_builddir)/poptrie_test_basic

