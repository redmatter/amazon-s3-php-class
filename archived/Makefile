VERSION_FILE=../version_info
include /usr/share/librml/Makefile.macros

RML_DATABASE_MYSQL_GIT_SUBMODULE=libs/rml-database-mysql
RML_DATABASE_MYSQL_DIR=../$(RML_DATABASE_MYSQL_GIT_SUBMODULE)
RML_DATABASE_MYSQL_HPP=$(RML_DATABASE_MYSQL_DIR)/rml/database/mysql/connection.hpp

RML_UTILS_GIT_SUBMODULE=libs/rml-utils
RML_UTILS_DIR=../$(RML_UTILS_GIT_SUBMODULE)
RML_UTILS_HPP=$(RML_UTILS_DIR)/rml/utils/enabled_disabled_list.hpp

include ../common/Makefile.macros

LIBS += -L../common/$(TMP)
# CPPFLAGS += -Wsign-promo

TARGET=archived

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a

# set RML_LOG_SOURCE_LOCATION=NO from make command line to disable source-location in log lines
RML_LOG_SOURCE_LOCATION=YES

CXX_INCLUDES += -isystem /usr/include/threadpool \
	-I $(RML_DATABASE_MYSQL_DIR) \
	-I $(RML_UTILS_DIR) \

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

CPPFLAGS += -DRML_DATABASE_MYSQL_DEBUG_ENABLED

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DRML_CONFIG_ENABLE_HAPI
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

CPPFLAGS += -DRETENTION_PURGE_SQL_DEBUG

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=ArchiveD
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/archiving/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=rm-archived
CPPFLAGS += -DSYSLOG_APP_NAME='"$(SYSLOG_APP_NAME)"'

PID_FILE_PATH=/var/run/archiving/rm$(TARGET).pid
CPPFLAGS += -DPID_FILE_PATH='"$(PID_FILE_PATH)"'

LIBRML_VERSION=1.1
CPPFLAGS += $(shell pkg-config rml-$(LIBRML_VERSION) --cflags)

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_chrono$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lcrypto -lrt\
	-lcryptopp -lpugixml -lmysqlpp -ls3 $(shell pkg-config rml-$(LIBRML_VERSION) --libs) -larchiving_common -lfcgi \
	$(MYSQLCLIENT_LIBS_DIR) -lmysqlclient

CXX_SRCS += \
	retention_purge.cpp \
	main.cpp

C_SRCS += \

BUILD_MODE=DEBUG

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target
include ../external/Makefile.cpp-netlib
# include ../external/Makefile.cryptopp

main.cpp: $(RML_DATABASE_MYSQL_HPP) $(RML_UTILS_HPP)

ifeq ($(RML_DATABASE_MYSQL_DIR),../$(RML_DATABASE_MYSQL_GIT_SUBMODULE))
$(RML_DATABASE_MYSQL_HPP):
	@cd .. && git submodule update --init --recursive $(RML_DATABASE_MYSQL_GIT_SUBMODULE)
endif

ifeq ($(RML_UTILS_DIR),../$(RML_UTILS_GIT_SUBMODULE))
$(RML_UTILS_HPP):
	@cd .. && git submodule update --init --recursive $(RML_UTILS_GIT_SUBMODULE)
endif

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION)

clean_extra: $(CLEAN_EXTRA)
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) clean

