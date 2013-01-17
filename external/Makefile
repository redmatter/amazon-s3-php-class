SQLITE_VERSION=3071502

.PHONY: sqlite clean

sqlite: prefix=$(shell pwd)/$@
sqlite: sqlite/sqlite-autoconf-$(SQLITE_VERSION)
	prefix=$(shell pwd)/sqlite
	@cd sqlite/sqlite-autoconf-$(SQLITE_VERSION) && \
		./configure --enable-static --disable-shared \
			--bindir=$(prefix)/bin --libdir=$(prefix)/lib \
			--includedir=$(prefix)/include --prefix=$(prefix) && \
		make install

sqlite/sqlite-autoconf-$(SQLITE_VERSION):
	@mkdir -p sqlite && tar -C sqlite -xzf sqlite-autoconf-$(SQLITE_VERSION).tar.gz

clean:
	rm -rf sqlite
