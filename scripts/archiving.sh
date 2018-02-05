#!/bin/sh

services="rmarchived rmcollectd rmcollect-smsd rmcollect-importd rmarchpolfix rmarchexport rmarchpcapimport"

for s in $services; do
	[ -x /etc/init.d/$s ] && service $s $1
done

