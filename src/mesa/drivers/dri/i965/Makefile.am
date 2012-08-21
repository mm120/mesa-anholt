

# Copyright © 2011 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

include Makefile.sources

# Hack to make some of the non-automake variables work.
TOP=$(top_builddir)

AM_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/ \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa/ \
	-I$(top_srcdir)/src/mesa/drivers/dri/common \
	-I$(top_srcdir)/src/mesa/drivers/dri/intel \
	-I$(top_srcdir)/src/mesa/drivers/dri/intel/server \
	$(DEFINES) \
	$(API_DEFINES) \
	$(INTEL_CFLAGS)

AM_CXXFLAGS = $(AM_CFLAGS)

dridir = $(DRI_DRIVER_INSTALL_DIR)

if HAVE_I965_DRI
dri_LTLIBRARIES = i965_dri.la
endif

i965_dri_la_SOURCES = \
	$(i965_C_FILES) \
	$(i965_CXX_FILES)

if HAVE_LLVM
noinst_LTLIBRARIES = libi965_llvm.la
libi965_llvm_la_SOURCES = \
	brw_fs_llvm.cpp \
	gen_frame_lowering.cpp \
	gen_frame_lowering.h \
	gen_instr_info.cpp \
	gen_instr_info.h \
	gen_isel_dag_to_dag.cpp \
	gen_mc_asm_info.cpp \
	gen_mc_asm_info.h \
	gen_mc_target_desc.cpp \
	gen_register_info.cpp \
	gen_register_info.h \
	gen_subtarget.cpp \
	gen_subtarget.h \
	gen_target_lowering.cpp \
	gen_target_lowering.h \
	gen_target_machine.cpp \
	gen_target_machine.h

libi965_llvm_la_CFLAGS = $(AM_CFLAGS) $(LLVM_CFLAGS)
libi965_llvm_la_CXXFLAGS = $(AM_CXXFLAGS) $(LLVM_CXXFLAGS) \
        -UDEBUG \
        -Wno-overloaded-virtual \
        -Wno-cast-qual
I965_LLVM_LIBADD = libi965_llvm.la
I965_LLVM_LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) `$(LLVM_CONFIG) --libs ipo`

BUILT_SOURCES = \
	gen_asm_writer.h.inc \
	gen_calling_conv.h.inc \
	gen_code_emitter.h.inc \
	gen_dag_isel.h.inc \
	gen_instr_info.h.inc \
	gen_register_info.h.inc \
	gen_subtarget_info.h.inc

TABLEGEN = $(AM_V_GEN)$(LLVM_BINDIR)/llvm-tblgen -I $(LLVM_INCLUDEDIR)

gen_asm_writer.h.inc: *.td
	$(TABLEGEN) -gen-asm-writer gen.td -o $@ || (rm -f $@; exit 1)
gen_calling_conv.h.inc: *.td
	$(TABLEGEN) -gen-callingconv gen.td -o $@ || (rm -f $@; exit 1)
gen_code_emitter.h.inc: *.td
	$(TABLEGEN) -gen-emitter gen.td -o $@ || (rm -f $@; exit 1)
gen_dag_isel.h.inc: *.td
	$(TABLEGEN) -gen-dag-isel gen.td -o $@ || (rm -f $@; exit 1)
gen_instr_info.h.inc: *.td
	$(TABLEGEN) -gen-instr-info gen.td -o $@ || (rm -f $@; exit 1)
gen_register_info.h.inc: *.td
	$(TABLEGEN) -gen-register-info gen.td -o $@ || (rm -f $@; exit 1)
gen_subtarget_info.h.inc: *.td
	$(TABLEGEN) -gen-subtarget gen.td -o $@ || (rm -f $@; exit 1)

else
AM_CFLAGS += -DNO_LLVM
AM_CXXFLAGS += -DNO_LLVM
endif

i965_dri_la_LDFLAGS = -module -avoid-version -shared \
        $(I965_LLVM_LDFLAGS)

i965_dri_la_LIBADD = \
	$(DRI_LIB_DEPS) \
	$(INTEL_LIBS) \
	$(I965_LLVM_LIBADD) \
	../common/libdricommon.la

# Provide compatibility with scripts for the old Mesa build system for
# a while by putting a link to the driver into /lib of the build tree.
all-local: i965_dri.la
	$(MKDIR_P) $(top_builddir)/$(LIB_DIR);
	ln -f .libs/i965_dri.so $(top_builddir)/$(LIB_DIR)/i965_dri.so;

CLEANFILES = $(BUILT_SOURCES)
