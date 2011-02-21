include ../version_info
include /usr/share/librml/Makefile.macros

FREESWITCH_DIR=/opt/freeswitch

CXX_INCLUDES += -I$(FREESWITCH_DIR)/include
C_INCLUDES += -I$(FREESWITCH_DIR)/include
LIBS += -L$(FREESWITCH_DIR)/lib \
		-L../common/$(TMP)

TARGET=archived

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a

# set RML_LOG_SOURCE_LOCATION=NO from make command line to disable source-location in log lines
RML_LOG_SOURCE_LOCATION=YES

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=ArchiveD
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/rm$(TARGET).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-archived
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=/var/run/archiving/rm$(TARGET).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

ARCHIVED_LISTEN_PORT=10100
CPPFLAGS += -DARCHIVED_LISTEN_PORT='"$(ARCHIVED_LISTEN_PORT)"'

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_log$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lcrypt -luuid -laws\
	-lrml -lpugixml -larchiving_common

CXX_SRCS += \
	main.cpp

C_SRCS += \

BUILD_MODE=DEBUG

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common $(MAKEFLAGS) BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION)

clean_extra:
	@$(MAKE) -C ../common $(MAKEFLAGS) BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) clean

