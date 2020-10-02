# default; override from commandline
BUILD_MODE=DEBUG
VERSION_FILE=../../version_info

include ../../libs/rml/rml/Makefile.macros

CPPFLAGS += -D__librml__tmp_fstream__constructor_private__
CPPFLAGS += -DRML_LOGGER_SYSLOG_ONLY
CPPFLAGS += -DRML_CONFIG_ENABLE_HAPI
CPPFLAGS += -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
CPPFLAGS += -DBOOST_CHRONO_VERSION=2

DEFAULT_LOG_FILE_PATH=console
CPPFLAGS += -DDEFAULT_LOG_FILE_PATH='"$(DEFAULT_LOG_FILE_PATH)"'

CXX_INCLUDES += -I../../common

TARGET=poc-archived-client

LIBS += \
	-lboost_thread \
	-lboost_system \
	-lboost_chrono \
	-lboost_random \
	-lboost_filesystem \
	-lboost_regex \
	-lpugixml -lcryptopp -L ../../common -larchiving_common

CXX_SRCS += \
	main.cpp

C_SRCS += \


include ../../libs/rml/rml/Makefile.rules
include ../../libs/rml/rml/Makefile.app_target
include ../../external/Makefile.cpp-netlib

include ../../libs/Makefile.rml
include ../../libs/Makefile.rml-queue
include ../../libs/Makefile.rml-utils
