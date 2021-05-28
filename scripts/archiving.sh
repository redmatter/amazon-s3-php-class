#!/bin/bash

services=(
	rmestuary
	rmarchived
	rmcollectd
	rmcollect-smsd
	# rmcollect-importd
	rmarchpolfix
	rmarchexport
	rmarchpcapimport
	rmarchdsrpreproc
)

for s in "${services[@]}"; do
	if [ -x "/etc/init.d/$s" ]; then
		/sbin/service "$s" "$1"
	fi
done
