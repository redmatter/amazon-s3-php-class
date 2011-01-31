include /usr/share/librml/Makefile.macros
include ../version_info

TARGET=libarchiving_common.a

ifeq ($(LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DLOG_SOURCE_LOCATION
endif

LIBS = \

CXX_SRCS += \
		dir_monitor.cpp \
		archived.cpp

C_SRCS += \
		md5.c

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

$(TMP)/md5.o : md5.h

install: $(TMP)/$(TARGET)
	$(MKDIR) $(DESTDIR_LIB) $(DESTDIR_INCLUDE) $(DESTDIR_DATA)
	$(INSTALL_DATA) *.h $(DESTDIR_INCLUDE)
	$(INSTALL_DATA) Makefile.* $(DESTDIR_DATA)
	$(INSTALL_DATA) $(TMP)/$(TARGET) $(DESTDIR_LIB)

