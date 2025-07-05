TARGET   = snapper
SRC_CC   = main.cc ../lib/snapper.cc ../lib/archive.cc ../lib/backlink.cc
LIBS    += base vfs

INC_DIR += $(REP_DIR)/src/include
