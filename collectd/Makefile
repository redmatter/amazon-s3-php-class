# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../version_info

include /usr/share/librml/Makefile.macros
include ../common/Makefile.macros

FREESWITCH_DIR=/opt/freeswitch

CXX_INCLUDES += -I$(FREESWITCH_DIR)/include -I$(DEV_LIBS_BASEDIR)/usr/include/cpp-netlib
C_INCLUDES += -I$(FREESWITCH_DIR)/include
LIBS += -L$(FREESWITCH_DIR)/lib \
		-L../common/$(TMP)
# CPPFLAGS += -Wsign-promo
CPPFLAGS += -DRML_DEV_DEBUG

TARGET=collectd

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a \
	../external/sqlite/lib/libsqlite3.a

RML_LOG_SOURCE_LOCATION=YES


DATABASE_SQLITE=YES
ifeq ($(DATABASE_SQLITE),YES)

CPPFLAGS += -DDATABASE_SQLITE
CXX_INCLUDES += -I ../external/sqlite/include
LIBS += -L ../external/sqlite/lib -lsqlite3 -ldl

else # DATABASE_SQLITE

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
LIBS += -lmysqlpp -lmysqlclient_r
endif # MYSQLPP_MYSQL_HEADERS_BURIED

endif # DATABASE_SQLITE


ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving collect-d
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/archiving/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-$(TARGET)
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=/var/run/archiving/$(DEFAULT_APP_NAME).pid
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
	-lboost_regex$(BOOST_NAME_EXT) \
	$(shell pkg-config rml-$(LIBRML_VERSION) --libs) -lpugixml -lcryptopp -larchiving_common -luuid

CXX_SRCS += \
	main.cpp

C_SRCS += \

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target


../external/sqlite/lib/libsqlite3.a:
	@$(MAKE) -C ../external sqlite

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) DATABASE_SQLITE=$(DATABASE_SQLITE)

clean_extra:
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) DATABASE_SQLITE=$(DATABASE_SQLITE) clean
	@$(MAKE) -C ../external clean

