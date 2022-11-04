include ../../common/Makefile.macros

CXXFLAGS = -ggdb -O0 -std=gnu++98
# CXXFLAGS = -Ofast
# CXXFLAGS += --coverage

TMP=tmp-test-build

CPPFLAGS += -I /usr/include/cpp-netlib \
			-D inline_=inline -D CRYPTOPP_ENABLE_NAMESPACE_WEAK=1 \
			-D_GLIBCXX_USE_CXX11_ABI=0

LIBS_test_encrypted_header_v2 = \
	-lboost_system \
	-L ../../common/$(TMP) -larchiving_common

include $(LIBS_MAKEFILES_DIR)/Makefile.gtest

test_encrypted_header_v2 : version.o ../../common/$(TMP)/libarchiving_common.a
../../common/$(TMP)/libarchiving_common.a:
	@$(MAKE) TMP=$(TMP) -C ../../common/ all

