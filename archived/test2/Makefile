CXXFLAGS = -ggdb -O0
# CXXFLAGS = -Ofast
# CXXFLAGS += --coverage
CPPFLAGS += -DRML_DEV_DEBUG

CPPFLAGS_test_boost_iostreams_bz2_decompress_hang = \
	-DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1 \
	-isystem /usr/include/cpp-netlib

LIBS_test_boost_iostreams_bz2_decompress_hang = \
	-lboost_system \
	-lboost_iostreams \
	-lcryptopp

CPPFLAGS_test_purge_by_filetype_selection = \
	-DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1 \

LIBS_test_purge_by_filetype_selection = \
	../../common/libarchiving_common.a \
	-lboost_system \
	-lboost_iostreams \
	-lcryptopp \
	-lrml

include /usr/share/librml/Makefile.gtest
