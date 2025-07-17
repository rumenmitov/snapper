TARGET   = snappertests
SRC_CC   = main.cc snapper.cc archive.cc backlink.cc
LIBS    += base vfs

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/src/lib

ifdef TESTS
	CC_OPT += -DTESTS=$(TESTS)
else
	CC_OPT += -DTESTS=1000
endif
