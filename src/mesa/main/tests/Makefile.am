AM_CPPFLAGS = \
	-I$(top_builddir)/src/gtest/include \
	-I$(top_builddir)/src/mapi \
	-I$(top_builddir)/src/mesa \
	-I$(top_builddir)/include \
	$(X11_CFLAGS)
AM_CFLAGS = $(MESA_CFLAGS)

TESTS = main-test \
        threadpool-test
check_PROGRAMS = $(TESTS)

MESA_LIBS = \
	$(top_builddir)/src/mesa/libmesa.la \
	-lpthread

main_test_SOURCES =			\
	enum_strings.cpp
main_test_LDADD = \
        $(MESA_LIBS) \
	$(top_builddir)/src/gtest/libgtest.la

threadpool_test_SOURCES = threadpool-test.c
threadpool_test_LDADD = $(MESA_LIBS)
