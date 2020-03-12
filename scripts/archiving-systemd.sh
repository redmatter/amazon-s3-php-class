#!/usr/bin/env bash

services=(
	rmarchived.service
	rmcollectd.service
	rmcollect-smsd.service
	rmcollect-importd.service
	rmestuary.service
	rmarchpolfix.service
	rmarchexport.service
	rmarchpcapimport.service
	rmarchdsrpreproc.service
	eml2a-milter.service
)

services_present=()
for svc in "${services[@]}"; do
	if [ -e "/usr/lib/systemd/system/$svc" ]; then
		services_present+=("$svc")
	fi
done

if [ "${#services_present[@]}" -eq 0 ]; then
	echo "No archiving services found." 1>&2
	exit 1
fi

set -x
/usr/bin/env systemctl "$1" "${services_present[@]}"
