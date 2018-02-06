SQLITE_VERSION=3071502

CPP_NETLIB_VERSION=0.11.2-final
ifndef CPP_NETLIB_BUILD_TESTS
CPP_NETLIB_BUILD_TESTS=ON
endif
ifndef CPP_NETLIB_BUILD_EXAMPLES
CPP_NETLIB_BUILD_EXAMPLES=ON
endif

.PHONY: sqlite cpp-netlib clean

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

clean:
	rm -rf sqlite
	rm -rf cpp-netlib
