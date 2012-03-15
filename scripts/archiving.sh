#!/bin/sh

services="rmarchived rmcollectd rmcollect-smsd"

for s in $services; do
	[ -x /etc/init.d/$s ] && service $s $1
done

