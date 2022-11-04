# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../../version_info

include ../../common/Makefile.macros

include $(LIBS_MAKEFILES_DIR)/Makefile.macros

TARGET=$(notdir $(shell pwd))

TARGET_DEPS=../../common/$(TMP)/libarchiving_common.a

RML_LOG_SOURCE_LOCATION=YES

ifeq ($(RML_LOG_SOURCE_LOCATION),YES)
CPPFLAGS += -DRML_LOG_SOURCE_LOCATION
endif

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DBOOST_NETWORK_ENABLE_HTTPS

LIBS += -L../../common/$(TMP)

DEFAULT_APP_NAME=$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_LOG_FILE_PATH=syslog
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

LIBS += \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lboost_thread$(BOOST_NAME_EXT) \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_filesystem$(BOOST_NAME_EXT) \
	-lboost_date_time$(BOOST_NAME_EXT) \
	-lboost_iostreams$(BOOST_NAME_EXT) \
	-lcryptopp -lpugixml -larchiving_common -lcrypto -lssl

CXX_SRCS += \
	main.cpp

C_SRCS += \

include $(LIBS_MAKEFILES_DIR)/Makefile.rules
include $(LIBS_MAKEFILES_DIR)/Makefile.app_target
include ../../external/Makefile.cpp-netlib

include ../../libs/Makefile.rml

$(TMP)/extract : $(TMP)/extract.o
	g++ $(TMP)/version.c -o $@ $< $(BUILD_LFLAGS) $(LFLAGS)

$(TARGET) : $(TMP)/$(TARGET)
	rm -f $@ && ln -s $< $@

clean_extra: $(CLEAN_EXTRA)
