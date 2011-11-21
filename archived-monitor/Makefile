include ../version_info
include /usr/share/librml/Makefile.macros

TARGET=archived-monitor

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=ArchiveD Monitor
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

LIBS +=

CXX_SRCS += \
	main.cpp

C_SRCS += -lrt

BUILD_MODE=DEBUG

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target
