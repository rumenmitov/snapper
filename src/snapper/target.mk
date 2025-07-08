TARGET   = snapper
SRC_CC   = main.cc snapper.cc backlink.cc archive.cc
LIBS    += base vfs

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/src/lib
