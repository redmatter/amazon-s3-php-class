#!/bin/bash

set -e

find_tests() {
    find "${tests_dir}/" -type f -name "test_*cpp" | \
    while read test; do
        test_bin="${test%.cpp}"
        if [ ! -f "$test_bin" ]; then
            echo "$test_bin not found"
            exit 1
        fi

        if [ ! -x "$test_bin" ]; then
            echo "$test_bin not executable"
            exit 1
        fi
        echo "$test_bin"
    done
}

displayResultAndCleanup() {
    local res=$?
    if [ $res -eq 0 ]; then
        echo Tests run OK
    else
        echo Tests Failed
    fi

    if [ "${make_clean:-0}" = 1 ]; then
        make -C "$tests_dir" clean
    fi

    if [ "${chown_artefacts:-0}" = 1 ]; then
        local found_artefacts=( $(ls "$tests_dir"/*.{o,d,a} $(find_tests) 2>/dev/null) )
        if [ ${#found_artefacts} -gt 0 ]; then
            chown --reference="$0" "${found_artefacts[@]}" 
        fi
    fi

    for o in "${other_artefacts[@]}"; do
        chown --recursive --reference="$0" "$o"
    done

    exit $res
}

trap "displayResultAndCleanup" EXIT

: "${tests_dir:="$(dirname "$0")"}"

other_artefacts=("$tests_dir/../common/tmp-test-build" "$tests_dir/../common/libarchiving_common.a")

if [ $# -ge 1 ] && [ -d "$1" ]; then
    tests_dir="$1"
fi

make -C "$tests_dir"

for test in $(find_tests); do
    "$test"
done
