CXXFLAGS = -ggdb -O0
# CXXFLAGS = -Ofast
TESTS_CPPFLAGS += --coverage

LIBRML_VERSION=1.1

TESTS_CPPFLAGS += -DRML_DEV_DEBUG \
	-DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

CPPFLAGS_test_boost_iostreams_bz2_decompress_hang = \
	-isystem /usr/include/cpp-netlib

LIBS_test_boost_iostreams_bz2_decompress_hang = \
	-lboost_system \
	-lboost_iostreams \
	-lcryptopp

CPPFLAGS_test_purge_by_filetype_selection = \

LIBS_test_purge_by_filetype_selection = \
	../../common/libarchiving_common.a \
	-lboost_system \
	-lboost_iostreams \
	-lcryptopp $(shell pkg-config rml-$(LIBRML_VERSION) --libs)

CPPFLAGS_test_suppress_headers_check = \

LIBS_test_suppress_headers_check = \
	../../common/libarchiving_common.a \
	-lboost_system \
	-lboost_iostreams \
	-lboost_date_time \
	-lcryptopp $(shell pkg-config rml-$(LIBRML_VERSION) --libs)

include /usr/share/librml/Makefile.gtest
