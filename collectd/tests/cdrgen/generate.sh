#!/bin/bash

: ${data_dir:=$(dirname $0)}
: ${out_dir:=/var/log/freeswitch/xml_cdr}
: ${batch:=100}
[ -n "$1" ] && batch=$1

echo Generating $batch x 2 CDRs into $out_dir.

time=$(date +%s)
while [ "$batch" -gt 0 ]; do
	auuid=$(uuidgen);
	buuid=$(uuidgen);
	cat $data_dir/aLeg_uuid.xml \
		| sed s/aLeg_uuid/$auuid/ \
		| sed s/bLeg_uuid/$buuid/ \
		| sed s/startEpoch/$time/ \
		> $out_dir/$auuid.cdr.xml
	cat $data_dir/bLeg_uuid.xml \
		| sed s/aLeg_uuid/$auuid/ \
		| sed s/bLeg_uuid/$buuid/ \
		| sed s/startEpoch/$time/ \
		> $out_dir/$buuid.cdr.xml

	echo $auuid $buuid

	let batch--
done
