# default; override from commandline
BUILD_MODE=DEBUG

include ../version_info
include /usr/share/librml/Makefile.macros

TARGET=migrationd

RML_LOG_SOURCE_LOCATION=YES

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving migration-d
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-$(TARGET)
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=/var/run/archiving/$(DEFAULT_APP_NAME).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_log$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lrml -lpugixml -lmysqlpp -lcryptopp

CXX_SRCS += \
	main.cpp

C_SRCS += \

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target
