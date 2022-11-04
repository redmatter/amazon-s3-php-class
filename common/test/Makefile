# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../../version_info

include ../common/Makefile.macros
include $(LIBS_MAKEFILES_DIR)/Makefile.macros

TARGET=test

CXX_INCLUDES += -I$(DEV_LIBS_BASEDIR)/usr/include/cpp-netlib
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

LIBS += -L../$(TMP)

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY

DEFAULT_APP_NAME=test
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=ArchivingCommonTest
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=test.conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-archiving-common-test
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=$(TARGET).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

LIBRML_VERSION=1.1
CPPFLAGS += $(shell pkg-config rml-$(LIBRML_VERSION) --cflags)

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lcryptopp $(shell pkg-config rml-$(LIBRML_VERSION) --libs) -larchiving_common

CXX_SRCS += \

C_SRCS += \

include $(LIBS_MAKEFILES_DIR)/Makefile.rules
include $(LIBS_MAKEFILES_DIR)/Makefile.app_target

$(TMP)/main.o : main.cpp ../md5.hpp

