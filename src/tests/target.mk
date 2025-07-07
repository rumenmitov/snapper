TARGET   = snappertests
SRC_CC   = main.cc 
LIBS    += base vfs os rtc_session

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/src/lib
