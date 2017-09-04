#!/bin/bash

set -eu

: "${data_root_dir:="$(dirname "$0")"}"
: "${out_dir:=/var/log/freeswitch/xml_cdr}"
: "${batch:=100}"
[ -n "$1" ] && batch=$1

sample_data_types=(with-recording with-10mins-recording without-recording)

if ! which sox &>/dev/null; then
	echo "sox utility not found.. cannot continue"
	exit 1;
fi

whitenoise_wav="/tmp/whitenoise.wav"
make_whitenoise_wav() {
	local outfile="${1}"
	if [ -e "$outfile" ]; then
		return 0;
	fi

	# Option differences in v12.x v/s v14.x+
	# See http://sox.cvs.sourceforge.net/sox/sox/ChangeLog
	if sox --version &>/dev/null; then
		sox_opt=(-b 16)
	else
		sox_opt=(-w)
	fi

	sox -t sl -r 22050 -c 1 /dev/zero -r 22050 -c 1 "${sox_opt[@]}" "$outfile" \
		synth 10:00 whitenoise vol 0.6 fade q 10 10:00 10
}

combine_wavs() {
	if [ "$#" -lt 3 ]; then
		echo "combine_wavs: Invalid usage"
	fi

	sox "$@"
}

make_whitenoise_recording() {
	make_whitenoise_wav "$whitenoise_wav"
	combine_wavs "$1" "$whitenoise_wav" "$2"
}

echo "Generating $batch x 2 CDRs into $out_dir."

time=$(date +%s)
while [ "$batch" -gt 0 ]; do
	sample_type="${sample_data_types[$((${RANDOM:0:1} % 2))]}"
	data_dir="${data_root_dir}/${sample_type}"

	auuid=$(uuidgen);
	buuid=$(uuidgen);

	if [[ "$sample_type" = 'with-recording' ]]; then
		cp "$data_dir/aLeg_uuid.wav" "/tmp/1_2017_08_22_${auuid}.wav"
		cp "$data_dir/bLeg_uuid.wav" "/tmp/1_2017_08_22_${buuid}.wav"
	elif [[ "$sample_type" = 'with-10mins-recording' ]]; then
		make_whitenoise_recording "$data_dir/aLeg_uuid.wav" "/tmp/1_2017_08_22_${auuid}.wav"
		make_whitenoise_recording "$data_dir/bLeg_uuid.wav" "/tmp/1_2017_08_22_${buuid}.wav"
	fi

	sed -e "s/{{aLeg_uuid}}/$auuid/g" \
		-e "s/{{bLeg_uuid}}/$buuid/g" \
		-e "s/{{startEpoch}}/$time/g" \
		< "$data_dir/aLeg_uuid.xml" \
		> "$out_dir/$auuid.cdr.xml"
	sed -e "s/{{aLeg_uuid}}/$auuid/g" \
		-e "s/{{bLeg_uuid}}/$buuid/g" \
		-e "s/{{startEpoch}}/$time/g" \
		< "$data_dir/bLeg_uuid.xml" \
		> "$out_dir/$buuid.cdr.xml"

	echo "$auuid $buuid $sample_type"

	let batch--
done
