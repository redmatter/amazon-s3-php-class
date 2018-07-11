#!/bin/bash

services=(
	rmarchived
	rmcollectd
	rmcollect-smsd
	rmcollect-importd
	rmarchpolfix
	rmarchexport
	rmarchpcapimport
	rmarchdsrpreproc
)

for s in "${services[@]}"; do
	if [ -x "/etc/init.d/$s" ]; then
		service "$s" "$1"
	fi
done
