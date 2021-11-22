SQLITE_VERSION=3270200

CPP_NETLIB_VERSION=0.11.2-final
ifndef CPP_NETLIB_BUILD_TESTS
CPP_NETLIB_BUILD_TESTS=OFF
endif
ifndef CPP_NETLIB_BUILD_EXAMPLES
CPP_NETLIB_BUILD_EXAMPLES=OFF
endif

CRYPTOPP_VERSION=5.6.1

LIBS3_VER_MAJOR=4
LIBS3_VER_MINOR=1
LIBS3_VER=$(LIBS3_VER_MAJOR).$(LIBS3_VER_MINOR)
# latest of 'redmatter-master' branch within redmatter fork
LIBS3_GIT_HASH=3e35a9b
LIBS3_GIT_DOWNLOAD_URL=https://github.com/redmatter/libs3/tarball/$(LIBS3_GIT_HASH)

.PHONY: libs3 sqlite cpp-netlib cryptopp clean

sqlite: prefix=$(shell pwd)/$@
sqlite: sqlite/sqlite-autoconf-$(SQLITE_VERSION)
	cd sqlite/sqlite-autoconf-$(SQLITE_VERSION) && \
		./configure --enable-static --disable-shared --enable-static-shell \
			--disable-json1 --enable-readline --disable-dynamic-extensions \
			--bindir=$(prefix)/bin --libdir=$(prefix)/lib \
			--includedir=$(prefix)/include --prefix=$(prefix) && \
		make -j2 install
	# delete the source
	rm -rf sqlite/sqlite-autoconf-$(SQLITE_VERSION)

sqlite/sqlite-autoconf-$(SQLITE_VERSION):
	@mkdir -p sqlite && tar -C sqlite -xzf sqlite-autoconf-$(SQLITE_VERSION).tar.gz

cpp-netlib: prefix=$(shell pwd)/$@
cpp-netlib: cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION)
	cd cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION) && \
	mkdir build && cd build && \
	cmake \
		-DCPP-NETLIB_BUILD_EXAMPLES=$(CPP_NETLIB_BUILD_EXAMPLES) \
		-DCPP-NETLIB_BUILD_TESTS=$(CPP_NETLIB_BUILD_TESTS) \
		-DCMAKE_INSTALL_PREFIX:PATH=$(prefix) \
		-D_GLIBCXX_USE_CXX11_ABI=0 \
		.. && \
	make -j2 install
	# delete the source
	rm -rf cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION)

cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION):
	@mkdir -p "$@" && tar -C "$@" --strip-components=1 -xzf cpp-netlib-$(CPP_NETLIB_VERSION).tar.gz

cryptopp: prefix=$(shell pwd)/$@
cryptopp: cryptopp/cryptopp-$(CRYPTOPP_VERSION)
	cd cryptopp/cryptopp-$(CRYPTOPP_VERSION) && \
	rm -f GNUmakefile && \
	TMPDIR=/tmp patch -p1 < ../../cryptopp-5.6.1-autotools.patch && \
	CXX=$(CXX) CC=$(CC) CXXFLAGS=-maes autoreconf --verbose --force --install && \
	CXX=$(CXX) CC=$(CC) CXXFLAGS=-maes ./configure --disable-shared --enable-static && \
	sed -i 's/`gcc /`$(CC) /g' libtool && \
	CXX=$(CXX) CC=$(CC) CXXFLAGS=-maes make -j2 && \
	CXX=$(CXX) CC=$(CC) CXXFLAGS=-maes make install DESTDIR="$(prefix)" INSTALL="install -p -c "
	# delete the source
	rm -rf cryptopp/cryptopp-$(CRYPTOPP_VERSION)

cryptopp/cryptopp-$(CRYPTOPP_VERSION):
	@mkdir -p "$@" && tar -C "$@" --strip-components=1 -xjvf cryptopp-$(CRYPTOPP_VERSION).tar.bz2

libs3: libs3/libs3-$(LIBS3_VER)-$(LIBS3_GIT_HASH)
	BUILD=libs3/build DESTDIR=.. make -C $< install VERBOSE=ON INSTALL=install && \
	rm -vrf libs3/build && \
	rm -vrf $< && \
	rm -vf libs3/lib/libs3.a && \
	sed -i 's/#define LIBS3_H/#define LIBS3_H\n\n#define LIBS3_VER_MAJOR $(LIBS3_VER_MAJOR)\n#define LIBS3_VER_MINOR $(LIBS3_VER_MINOR)/' "libs3/include/libs3.h"

libs3/libs3-$(LIBS3_VER)-$(LIBS3_GIT_HASH): redmatter-libs3-$(LIBS3_GIT_HASH).tar.gz
	mkdir -p "$@" && \
	tar -C "$@" --strip-components=1 -xzf redmatter-libs3-$(LIBS3_GIT_HASH).tar.gz
	# && \
	# TMPDIR=/tmp patch -d "$@" -p0 < libs3-fix_compilation_error_for_mixed_sign_comparison.patch

redmatter-libs3-$(LIBS3_GIT_HASH).tar.gz:
	wget -O "$@" $(LIBS3_GIT_DOWNLOAD_URL)

# Alias tar-gz target for easy invocation after updating the hash
.PHONY: libs3-download-tar-gz
libs3-download-tar-gz: libs3-$(LIBS3_GIT_HASH).tar.gz

clean:
	rm -rf sqlite
	rm -rf cpp-netlib
	rm -rf cryptopp
	rm -rf libs3
