#!/bin/bash

set -eu

if [ "${bash_debug:-0}" = 1 ]; then
    set -x
fi

: "${data_root_dir:="$(dirname "$0")/data"}"
: "${out_dir:=/var/log/freeswitch/xml_cdr}"

: "${batch:=100}"
dv_selected=(all)

all_enabled_data_variations=(
    with-recording
    with-recording-10mins
    without-recording
    with-call-analysis
    with-call-analysis-fudge
)

whitenoise_wav=()
trap '{ [ ${#whitenoise_wav[@]} -gt 0 ] && rm "${whitenoise_wav[@]}"; }' EXIT

make_temp_wav_path() {
    local filename
    filename="$(mktemp -p /tmp arcgen-XXXXXXXX)"
    mv "$filename" "${filename}.wav"
    echo -n "${filename}.wav"
}

make_minutes_whitenoise_wav() {
    if ! is_numeric "$1"; then
        error "Minute must be a number";
        return 1
    fi

    # Option differences in v12.x v/s v14.x+
    # See http://sox.cvs.sourceforge.net/sox/sox/ChangeLog
    if sox --version &>/dev/null; then
        sox_opt=(-b 16)
    else
        sox_opt=(-w)
    fi

    local outfile
    outfile="$(make_temp_wav_path)"
    sox -t sl -r 22050 -c 1 /dev/zero -r 22050 -c 1 "${sox_opt[@]}" "$outfile" \
        synth "${1}:00" whitenoise vol 0.6 fade q 10 "${1}:00" 10

    echo "$outfile"
}

combine_wavs() {
    if [ "$#" -lt 3 ]; then
        echo "combine_wavs: Invalid usage"
    fi

    sox "$@"
}

make_whitenoise_recording() {
    local minutes="$1"; shift

    if ! in_array "$minutes" "${!whitenoise_wav[@]}" || [ ! -r "${whitenoise_wav[$minutes]}" ]; then
        whitenoise_wav[$minutes]="$(make_minutes_whitenoise_wav "$minutes")"
    fi

    combine_wavs "$1" "${whitenoise_wav[$minutes]}" "$2"
}

log() {
    local tag="$1"; shift
    echo "$tag - $*"
}

error() {
    log ERROR "$@"
}

is_numeric() {
    [[ "$1" =~ ^[0-9]+$ ]] || [[ "$1" =~ ^[0-9]+\.[0-9]+$ ]] || [[ "$1" =~ ^\.[0-9]+$ ]]
}

in_array() {
    local needle="${1:-}"; shift
    if [ $# -le 0 ]; then
        return 1;
    fi

    for ((i = 1; i <= $#; ++i)); do
        if [ "${!i}" = "$needle" ]; then
            return 0;
        fi
    done

    return 1
}

is_valid_variation() {
    [ -d "$1" ] && [ -r "$1" ] &&
        [ -r "${1}/aLeg_uuid.xml" ] && [ -r "${1}/bLeg_uuid.xml" ]
}

usage_help() {
    [ -z "$*" ] || error "$*"$'\n'
    cat <<HELP
CollectD test data generator

 +- IMPORTANT -----------------------------------------------+
 | This script must be run as root user or with sudo prefix. |
 +-----------------------------------------------------------+

Usage:
    $(basename "$0") [--batch=<BatchSize>] [--data-variations=<DataVariations>] [--list-data-variations] [--help]

Options:
    -b <BatchSize>, --batch=<BatchSize>
        Number of leg-pairs (only A and B for now) / samples to create.  Default Value is 100

    -d <DataVariations>, --data-variations=<DataVariations>
        List of data-variations separated by commas. The default value is 'all' which signifies all that is available.
        You    can see a list of available options using the option --list-data-variations.

    -l, --list-data-variations
        List all data variations available.

    -h, --help
        This help.

HELP
    [ -z "$*" ] || exit 1
}

if ! which sox &>/dev/null; then
    error "sox utility not found.. cannot continue"
    exit 1;
fi

if [ ! -w "$out_dir" ]; then
    error "Cannot write to $out_dir"
    exit 1;
fi

short_options='b:d:lh'
long_options='batch:,data-variations:,list-data-variations,help'
cli_opts="$(getopt -n "$(basename "$0")" -o "${short_options}" --long "${long_options}" -- "$@")" || {
    usage_help "Invalid usage"
}
eval "cli_opts=( ${cli_opts} )"
for ((i = 0; i < ${#cli_opts[@]}; ++i)); do
    case "${cli_opts[$i]}" in
        -b|--batch)
            batch="${cli_opts[$((++i))]}"
            if ! is_numeric "$batch"; then
                usage_help "Invalid batch size"
            fi
            ;;

        -d|--data-variations)
            dv_selected=()
            IFS=',' read -r -a dv_selected <<< "${cli_opts[$((++i))]}"
            for ((v=0; v<${#dv_selected[@]}; ++v)); do
                if [ "${dv_selected[$v]}" = all ]; then
                    dv_selected=(all)
                    break;
                fi

                if ! is_valid_variation "${data_root_dir}/${dv_selected[$v]}"; then
                    usage_help "Invalid data-variation '${dv_selected[$v]}'"
                fi
            done
            ;;

        -l|--list-data-variations)
            echo "Available data-variations are"
            printf '    %s\n' "${all_enabled_data_variations[@]}"
            exit 0
            ;;

        -h|--help)
            usage_help
            exit 0
            ;;
        --)
            ;;
        *)
            usage_help "Invalid usage"
            ;;
    esac
done

if [ ${#dv_selected[@]} -eq 0 ] || [ "${dv_selected[0]}" = all ]; then
    dv_selected=("${all_enabled_data_variations[@]}")
fi

echo "Generating $batch x 2 CDRs into $out_dir."

time=$(date +%s)
while [ "$batch" -gt 0 ]; do
    variation="${dv_selected[$((${RANDOM:0:1} % ${#dv_selected[@]}))]}"
    data_dir="${data_root_dir}/${variation}"

    auuid=$(uuidgen);
    buuid=$(uuidgen);

    if [[ "$variation" =~ -([0-9]+)mins-whitenoise ]]; then
        minutes="${BASH_REMATCH[1]}"
        make_whitenoise_recording "$minutes" "$data_dir/aLeg_uuid.wav" "/tmp/1_2017_08_22_${auuid}.wav"
        make_whitenoise_recording "$minutes" "$data_dir/bLeg_uuid.wav" "/tmp/1_2017_08_22_${buuid}.wav"
    else
        if [ -r "${data_dir}/aLeg_uuid.wav" ]; then
            cp "$data_dir/aLeg_uuid.wav" "/tmp/1_2017_08_22_${auuid}.wav"
        fi
        if [ -r "${data_dir}/bLeg_uuid.wav" ]; then
            cp "$data_dir/bLeg_uuid.wav" "/tmp/1_2017_08_22_${buuid}.wav"
        fi
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

    echo "$auuid $buuid $variation"

    let batch--
done
