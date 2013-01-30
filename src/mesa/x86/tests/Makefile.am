AM_CFLAGS = \
	$(X11_CFLAGS) \
	$(PTHREAD_CFLAGS)
AM_CPPFLAGS = \
	-I$(top_builddir)/src/gtest/include \
	-I$(top_builddir)/src/mapi \
	-I$(top_builddir)/src/mesa \
	-I$(top_builddir)/include \
	$(API_DEFINES) $(DEFINES) $(INCLUDE_DIRS)

if HAVE_X86_ASM
TESTNAME = movnt-test
else
if HAVE_X86_64_ASM
TESTNAME = movnt-test
endif
endif

TESTS = $(TESTNAME)
check_PROGRAMS = $(TESTNAME)

movnt_test_SOURCES = \
	test_movnt.cpp \
        ../movnt.c

movnt_test_LDADD = \
	$(top_builddir)/src/gtest/libgtest.la \
        $(PTHREAD_LIBS)
