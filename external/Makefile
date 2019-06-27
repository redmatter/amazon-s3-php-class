SQLITE_VERSION=3270200

CPP_NETLIB_VERSION=0.11.2-final
ifndef CPP_NETLIB_BUILD_TESTS
CPP_NETLIB_BUILD_TESTS=ON
endif
ifndef CPP_NETLIB_BUILD_EXAMPLES
CPP_NETLIB_BUILD_EXAMPLES=ON
endif

CRYPTOPP_VERSION=5.6.1

.PHONY: sqlite cpp-netlib cryptopp clean

sqlite: prefix=$(shell pwd)/$@
sqlite: sqlite/sqlite-autoconf-$(SQLITE_VERSION)
	prefix=$(shell pwd)/sqlite
	cd sqlite/sqlite-autoconf-$(SQLITE_VERSION) && \
		./configure --enable-static --disable-shared \
			--bindir=$(prefix)/bin --libdir=$(prefix)/lib \
			--includedir=$(prefix)/include --prefix=$(prefix) && \
		make install

sqlite/sqlite-autoconf-$(SQLITE_VERSION):
	@mkdir -p sqlite && tar -C sqlite -xzf sqlite-autoconf-$(SQLITE_VERSION).tar.gz

cpp-netlib: prefix=$(shell pwd)/$@
cpp-netlib: cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION)
	pushd cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION) && \
	mkdir build && cd build && \
	cmake \
		-DCPP-NETLIB_BUILD_EXAMPLES=$(CPP_NETLIB_BUILD_EXAMPLES) \
		-DCPP-NETLIB_BUILD_TESTS=$(CPP_NETLIB_BUILD_TESTS) \
		-DCMAKE_INSTALL_PREFIX:PATH=$(prefix) \
		.. && \
	make -j2 install
	# delete the source
	rm -rf cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION)

cpp-netlib/cpp-netlib-$(CPP_NETLIB_VERSION):
	@mkdir -p "$@" && tar -C "$@" --strip-components=1 -xzf cpp-netlib-$(CPP_NETLIB_VERSION).tar.gz

cryptopp: prefix=$(shell pwd)/$@
cryptopp: cryptopp/cryptopp-$(CRYPTOPP_VERSION)
	pushd cryptopp/cryptopp-$(CRYPTOPP_VERSION) && \
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

clean:
	rm -rf sqlite
	rm -rf cpp-netlib
	rm -rf cryptopp
