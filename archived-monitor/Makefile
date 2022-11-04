VERSION_FILE=../version_info
include ../common/Makefile.macros
include $(LIBS_MAKEFILES_DIR)/Makefile.macros

TARGET=archived-monitor

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY

DEFAULT_APP_NAME=rm$(TARGET)
CPPFLAGS += -DDEFAULT_APP_NAME='"$(DEFAULT_APP_NAME)"'

DEFAULT_APP_LONG_NAME=ArchiveD Monitor
CPPFLAGS += -DDEFAULT_APP_LONG_NAME='"$(DEFAULT_APP_LONG_NAME)"'

LIBS += \
	-lboost_system$(BOOST_NAME_EXT) \
	-lboost_program_options$(BOOST_NAME_EXT) \
	-lrt \

CXX_SRCS += \
	main.cpp

C_SRCS += \

BUILD_MODE=DEBUG

include $(LIBS_MAKEFILES_DIR)/Makefile.rules
include $(LIBS_MAKEFILES_DIR)/Makefile.app_target

include ../libs/Makefile.rml
