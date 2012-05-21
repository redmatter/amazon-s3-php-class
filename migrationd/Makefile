include ../version_info
include /usr/share/librml/Makefile.macros

LIBS += -L../common/$(TMP)

TARGET=migrationd

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a

RML_LOG_SOURCE_LOCATION=YES

CXX_INCLUDES += -I$(DEV_LIBS_BASEDIR)/usr/include/cpp-netlib

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
	CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving migration-d
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
	-lboost_log$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lcppnetlib-uri-parsers \
	-lrml -lpugixml -lmysqlpp -lcryptopp -ls3 -lmysqlclient_r

CXX_SRCS += \
	main.cpp

C_SRCS += \

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION)

clean_extra:
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) clean

