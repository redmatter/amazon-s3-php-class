VERSION_FILE=../version_info

include /usr/share/librml/Makefile.macros
include Makefile.macros

TARGET=libarchiving_common.a

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

DATABASE_SQLITE=YES
ifeq ($(DATABASE_SQLITE),YES)

CPPFLAGS += -DDATABASE_SQLITE
CXX_INCLUDES += -I ../external/sqlite/include
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

DESTDIR=$(TMP)/build
DESTDIR_LIB=$(DESTDIR)/lib64
DESTDIR_INCLUDE=$(DESTDIR)/usr/include/rml
DESTDIR_DATA=$(DESTDIR)/usr/share/librml

INSTALL=install -c -m 755
INSTALL_DATA=install -c -m 644
MKDIR=mkdir -p

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.lib_target
include ../external/Makefile.cpp-netlib

install: $(TMP)/$(TARGET)
	$(MKDIR) $(DESTDIR_LIB) $(DESTDIR_INCLUDE) $(DESTDIR_DATA)
	$(INSTALL_DATA) *.h $(DESTDIR_INCLUDE)
	$(INSTALL_DATA) Makefile.* $(DESTDIR_DATA)
	$(INSTALL_DATA) $(TMP)/$(TARGET) $(DESTDIR_LIB)

../external/sqlite/include/sqlite3.h:
	@$(MAKE) -C ../external sqlite
#
#clean_extra:
#	@$(MAKE) -C ../external clean
