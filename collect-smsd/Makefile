# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../version_info

include ../libs/rml/rml/Makefile.macros
include ../common/Makefile.macros

FREESWITCH_DIR=/opt/freeswitch

CXX_INCLUDES += -I$(FREESWITCH_DIR)/include
C_INCLUDES += -I$(FREESWITCH_DIR)/include
LIBS += -L$(FREESWITCH_DIR)/lib \
		-L../common/$(TMP)
# CPPFLAGS += -Wsign-promo

TARGET=collect-smsd

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a

RML_LOG_SOURCE_LOCATION=YES

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
	CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving collect-smsd
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/archiving/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-$(TARGET)
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=/var/run/archiving/$(DEFAULT_APP_NAME).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_chrono$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lpugixml -lmysqlpp -lcryptopp -larchiving_common -lmysqlclient

# -lcrypt -luuid \

CXX_SRCS += \
	main.cpp

C_SRCS += \

include ../libs/rml/rml/Makefile.rules
include ../libs/rml/rml/Makefile.app_target
include ../external/Makefile.cpp-netlib

include ../libs/Makefile.rml

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION)

clean_extra: $(CLEAN_EXTRA)
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) clean

