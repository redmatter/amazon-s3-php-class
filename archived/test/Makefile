# default; override from commandline
BUILD_MODE=DEBUG

include ../libs/rml/rml/Makefile.macros
include ../../version_info

TARGET=test

RML_LOG_SOURCE_LOCATION=YES

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY

FREESWITCH_DIR=/opt/freeswitch

CXX_INCLUDES += -I$(FREESWITCH_DIR)/include
C_INCLUDES += -I$(FREESWITCH_DIR)/include
LIBS += -L$(FREESWITCH_DIR)/lib

DEFAULT_APP_NAME=test
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=CollectDTest
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=test.conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-collectd-test
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=$(TARGET).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

LIBRML_VERSION=1.1
CPPFLAGS += $(shell pkg-config rml-$(LIBRML_VERSION) --cflags)

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	$(shell pkg-config rml-$(LIBRML_VERSION) --libs) -lpugixml -lmysqlpp -lcryptopp

CXX_SRCS += \
	main.cpp

C_SRCS += \

include ../libs/rml/rml/Makefile.rules
include ../libs/rml/rml/Makefile.app_target

$(TMP)/extract : $(TMP)/extract.o
	g++ $(TMP)/version.c -o $@ $< $(BUILD_LFLAGS) $(LFLAGS)

extract : $(TMP)/extract
	rm -f $@ && ln -s $< $@
