# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../version_info

include /usr/share/librml/Makefile.macros

RML_UTILS_GIT_SUBMODULE=libs/rml-utils
RML_UTILS_DIR=../$(RML_UTILS_GIT_SUBMODULE)
RML_UTILS_HPP=$(RML_UTILS_DIR)/rml/utils/snowflake.hpp

RML_QUEUE_GIT_SUBMODULE=libs/rml-queue
RML_QUEUE_DIR=../$(RML_QUEUE_GIT_SUBMODULE)
RML_QUEUE_HPP=$(RML_QUEUE_DIR)/rml/queue/queue.hpp

RML_STATISTICS_GIT_SUBMODULE=libs/rml-statistics
RML_STATISTICS_DIR=../$(RML_STATISTICS_GIT_SUBMODULE)
RML_STATISTICS_HPP=$(RML_STATISTICS_DIR)/rml/statistics/time_moving_average.hpp

LIBS += 

TARGET=rmestuary

TARGET_DEPS=

CXX_INCLUDES += \
	-I /usr/include/threadpool \
	-I ../cpp-common \
	-I $(RML_UTILS_DIR) \
	-I $(RML_QUEUE_DIR) \
	-I $(RML_STATISTICS_DIR)

CPPFLAGS += -DBOOST_CHRONO_VERSION=2

include ../common/Makefile.macros

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
	CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

RML_LOG_SOURCE_LOCATION=YES
ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DRML_CONFIG_ENABLE_HAPI
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
CPPFLAGS += -DBOOST_CHRONO_VERSION=2
CPPFLAGS += -DRML_LOG_RUNTIME_LEVEL_CHANGE
CPPFLAGS += -DBOOST_NETWORK_HTTP_SERVER_CONNECTION_BUFFER_SIZE=4096

DEFAULT_APP_NAME=$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving Estuary
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/archiving/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-estuary
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
	-lboost_chrono$(BOOST_NAME_EXT) \
	-lboost_regex$(BOOST_NAME_EXT) \
	-lcryptopp -lpugixml \
	$(shell pkg-config rml-$(LIBRML_VERSION) --libs) \
	-L ../common/$(TMP)/ -larchiving_common

CXX_SRCS += \
	main.cpp

C_SRCS += \

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target
include ../external/Makefile.cpp-netlib

main.cpp: $(RML_UTILS_HPP) $(RML_QUEUE_HPP) $(RML_STATISTICS_HPP)

ifeq ($(RML_UTILS_DIR),../$(RML_UTILS_GIT_SUBMODULE))
$(RML_UTILS_HPP):
	@cd .. && git submodule update --init --recursive $(RML_UTILS_GIT_SUBMODULE)
endif

ifeq ($(RML_QUEUE_DIR),../$(RML_QUEUE_GIT_SUBMODULE))
$(RML_QUEUE_HPP):
	@cd .. && git submodule update --init --recursive $(RML_QUEUE_GIT_SUBMODULE)
endif

ifeq ($(RML_STATISTICS_DIR),../$(RML_STATISTICS_GIT_SUBMODULE))
$(RML_STATISTICS_HPP):
	@cd .. && git submodule update --init --recursive $(RML_STATISTICS_GIT_SUBMODULE)
endif

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) DATABASE_SQLITE=YES

clean_extra: $(CLEAN_EXTRA)
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) DATABASE_SQLITE=YES clean
