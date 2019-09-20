# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../../version_info

include ../../libs/rml/rml/Makefile.macros

TARGET=inotify

CXX_INCLUDES += -I../../libs/rml

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT)

CXX_SRCS += \
	main.cpp

C_SRCS += \

include ../../libs/rml/rml/Makefile.rules
include ../../libs/rml/rml/Makefile.app_target

