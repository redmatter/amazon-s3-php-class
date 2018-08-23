# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../version_info

include /usr/share/librml/Makefile.macros
include ../common/Makefile.macros

FREESWITCH_DIR=/opt/freeswitch

CXX_INCLUDES += -I$(FREESWITCH_DIR)/include -Imime
C_INCLUDES += -I$(FREESWITCH_DIR)/include
LIBS += -L$(FREESWITCH_DIR)/lib \
		-L../common/$(TMP) \
		-Lmime \
		-Lpugixml
# CPPFLAGS += -Wsign-promo

TARGET=eml2a

TARGET_DEPS=../common/$(TMP)/libarchiving_common.a mime/libmimedecoder.a

RML_LOG_SOURCE_LOCATION=YES

MYSQLPP_MYSQL_HEADERS_BURIED=YES
ifeq ($(MYSQLPP_MYSQL_HEADERS_BURIED),YES)
	CPPFLAGS += -DMYSQLPP_MYSQL_HEADERS_BURIED
endif

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
CPPFLAGS += -DRML_CONFIG_ENABLE_HAPI

DEFAULT_APP_NAME=$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=Archiving email to archive tool
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

CONFIG_FILE_PATH=/etc/archiving/$(DEFAULT_APP_NAME).conf
CPPFLAGS += -DCONFIG_FILE_PATH='"$(CONFIG_FILE_PATH)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

SYSLOG_APP_NAME=$(TARGET)
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
	$(shell pkg-config rml-$(LIBRML_VERSION) --libs) -lpugixml -lmysqlpp -lcryptopp -larchiving_common -lmimedecoder -ldb \
	$(MYSQLCLIENT_LIBS_DIR) -lmysqlclient

# -lcrypt -luuid \

CXX_SRCS += \
	main.cpp

C_SRCS += \

include /usr/share/librml/Makefile.rules
include /usr/share/librml/Makefile.app_target
include ../external/Makefile.cpp-netlib

../common/$(TMP)/libarchiving_common.a :
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION)

mime/libmimedecoder.a:
	@$(MAKE) -C mime
	
clean_extra: $(CLEAN_EXTRA)
	@$(MAKE) -C ../common BUILD_MODE=$(BUILD_MODE) TMP=$(TMP) RML_LOG_SOURCE_LOCATION=$(RML_LOG_SOURCE_LOCATION) clean

eml2a-milter: milter.c
	$(CC) -o $@ -I libmilter -g -O0 milter.c -lmilter -pthread

copytousr1:
	cp eml2a-milter /usr/bin
	chown postfix:postfix /usr/bin/eml2a-milter
	chmod 04755 /usr/bin/eml2a-milter

copytousr2:
	cp $(TMP)/eml2a /usr/bin/eml2a
	chown cdrmunch:cdrmunch /usr/bin/eml2a
	chmod 04755 /usr/bin/eml2a
	
