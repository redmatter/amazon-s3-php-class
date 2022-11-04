# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../../version_info

include ../../common/Makefile.macros
include $(LIBS_MAKEFILES_DIR)/Makefile.macros

TARGET=inotify

CXX_INCLUDES += -I../../libs/rml

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT)

CXX_SRCS += \
	main.cpp

C_SRCS += \

include $(LIBS_MAKEFILES_DIR)/Makefile.rules
include $(LIBS_MAKEFILES_DIR)/Makefile.app_target

