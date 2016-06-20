phar: vendor
	rm -rf artifacts
	php54 -d phar.readonly=off build.php

vendor: composer.phar
	./composer.phar install

composer.phar:
	curl -so composer.phar https://getcomposer.org/composer.phar
	chmod +x composer.phar

test: phar
	php54 test_phar.php && echo ' 'Ok || echo ' 'Failed

clean:
	rm -rf vendor composer.phar artifacts

.PHONY: build clean test phar

