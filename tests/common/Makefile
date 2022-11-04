include ../../common/Makefile.macros

CXXFLAGS = -ggdb -O0 -std=gnu++98
# CXXFLAGS = -Ofast
# CXXFLAGS += --coverage

TMP=tmp-test-build

CPPFLAGS += -I ../../external/cpp-netlib/include \
			-I ../../libs/rml \
			-D inline_=inline -D CRYPTOPP_ENABLE_NAMESPACE_WEAK=1 \
			-D_GLIBCXX_USE_CXX11_ABI=0

LIBS_test_archivable_channel = \
	-lboost_thread \
	-lboost_date_time \
	-lboost_system \
	-lboost_chrono \
	-L ../../common/$(TMP) -larchiving_common \
	-lcryptopp \

include $(LIBS_MAKEFILES_DIR)/Makefile.gtest

test_archivable_channel : ../../common/$(TMP)/libarchiving_common.a
../../common/$(TMP)/libarchiving_common.a:
	@$(MAKE) TMP=$(TMP) -C ../../common/ all
