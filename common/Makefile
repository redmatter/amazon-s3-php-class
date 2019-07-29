VERSION_FILE=../version_info

include ../libs/rml/rml/Makefile.macros
include Makefile.macros

TARGET=libarchiving_common.a

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

DATABASE_SQLITE=YES
ifeq ($(DATABASE_SQLITE),YES)

CPPFLAGS += -DDATABASE_SQLITE
CXX_INCLUDES += \
    -I ../external/sqlite/include
TARGET_DEPS= ../external/sqlite/include/sqlite3.h

else # DATABASE_SQLITE

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif # MYSQLPP_MYSQL_HEADERS_BURIED

endif # DATABASE_SQLITE

ifeq ($(LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DLOG_SOURCE_LOCATION
endif

LIBS = \

CXX_SRCS += \
		dir_monitor.cpp \
		defines.cpp \
		types.cpp \
		archived_client.cpp

C_SRCS += \

BUILD_MODE=DEBUG

include ../libs/rml/rml/Makefile.rules
include ../libs/rml/rml/Makefile.lib_target
include ../external/Makefile.cpp-netlib

include ../libs/Makefile.rml

../external/sqlite/include/sqlite3.h:
	$(MAKE) -C ../external sqlite
#
#clean_extra:
#	@$(MAKE) -C ../external clean
