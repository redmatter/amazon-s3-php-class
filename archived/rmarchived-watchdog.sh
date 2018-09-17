#!/bin/bash

: "${config_path:=/etc/archiving/rmarchived.conf}"
: "${pidfile:=/var/run/archiving/rmarchived-watchdog.pid}"
: "${hapi_hosttype:="$(/usr/bin/hapi get HostType)"}"

. /usr/share/rm-bash-functions/logging.sh
. /usr/share/rm-bash-functions/config.sh
. /usr/share/rm-bash-functions/pid-file.sh

freespace_KB_gt()
{
    if [ -d "$1" ]; then
        local op now
        op=($(/bin/df --output=avail "$1"))
        now="$(date +%s)"
        if [ "${now:$((${#now}-1)):1}" -eq 0 ]; then
            log info "${1} has ${op[1]} KB freespace"
        fi
        if [ "${op[1]}" -le "$2" ]; then
            log err "${1} does not have enough freespace (${op[1]} KB / ${2} KB)"
            return 1
        fi
    elif "${first:-true}"; then
        log warning "${1} does not exist"
    fi
}

# "in-process" sleep implementation using read; in order to reduce pid trashing
_sleep() {
    read -rt "${1?Specify sleep interval in seconds}" -u 1 <<<"" || return 0;
}

declare -A check_paths
add_check_path()
{
    local mount_point
    mount_point="$(stat -c '%m' "$1")"

    if [ ${check_paths["$mount_point"]+xxx} ]; then
        log notice "Merging check path; '$1' has mount-point '$mount_point'"
        check_paths["$mount_point"]=$((${check_paths["$mount_point"]}+$2))
    else
        log notice "Adding check path; '$1' has mount-point '$mount_point'"
        check_paths+=(["$mount_point"]="$2")
    fi
}

everything_ok()
{
    local path
    for path in "${!check_paths[@]}"; do
        if ! freespace_KB_gt "$path" "${check_paths[$path]}"; then
            return 1;
        fi
    done
}

terminate()
{
    log notice "Terminate requested"
    terminate_requested=true
}

cleanup()
{
    rm -f "$pidfile"
}

trap terminate TERM
trap cleanup EXIT

# log stderr at ERR level
exec 2> >(log_pipe err)

# Ensure that we can create a pidfile
if ! create_pid_file "$pidfile"; then
    exit 1
fi

# Setup ckecks
add_check_path "$(get_conf "$config_path" distiller_client.base_dir 0 /var/spool/cdrtmp/cdrmunch-cache)" \
    "$(get_conf "$config_path" distiller_client.base_dir.min_free_space_KB 0 500000)"

add_check_path "$(get_conf "$config_path" archived.cache_dir 0 /var/lib/archiving/cache)" \
    "$(get_conf "$config_path" archived.cache_dir.min_free_space_KB 0 500000)"

log notice "Watchdog started"
while ! "${terminate_requested:-false}"; do
    if ! everything_ok; then
        log err "Check(s) failed; terminating so that the ArchiveD is stopped"
        exit 1;
    fi

    # is first check? To avoid warning about things that can't change once running
    first=false

    _sleep 1
done
log notice "Watchdog stopped"

exit 0
