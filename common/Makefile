include /usr/share/librml/Makefile.macros
include ../version_info
include Makefile.macros

TARGET=libarchiving_common.a

CXX_INCLUDES += -I/usr/include/cpp-netlib

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
	CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

ifeq ($(LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DLOG_SOURCE_LOCATION
endif

LIBS = \

CXX_SRCS += \
		dir_monitor.cpp \
		defines.cpp \
		types.cpp \
		archived_client.cpp \
		archivable.cpp

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

install: $(TMP)/$(TARGET)
	$(MKDIR) $(DESTDIR_LIB) $(DESTDIR_INCLUDE) $(DESTDIR_DATA)
	$(INSTALL_DATA) *.h $(DESTDIR_INCLUDE)
	$(INSTALL_DATA) Makefile.* $(DESTDIR_DATA)
	$(INSTALL_DATA) $(TMP)/$(TARGET) $(DESTDIR_LIB)

