#!/bin/bash

: "${config_path:=/etc/archiving/rmarchived.conf}"
: "${pidfile:=/var/run/archiving/rmarchived-watchdog.pid}"
: "${hapi_hosttype:="$(/usr/bin/hapi get HostType)"}"

. /usr/share/rm-bash-functions/logging.sh
. /usr/share/rm-bash-functions/config.sh

freespace_KB_gt()
{
    if [ -d "$1" ]; then
        local op=($(/bin/df --output=avail "$1"))
        if [ $(("$(date +%s)" % 10)) -eq 0 ]; then
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

everything_ok()
{
    freespace_KB_gt "$(get_conf "$config_path" distiller_client.base_dir 0 /var/spool/cdrtmp/cdrmunch-cache)" \
        "$(get_conf "$config_path" distiller_client.base_dir.min_free_space_KB 0 500000)" && \
    freespace_KB_gt "$(get_conf "$config_path" archived.cache_dir 0 /var/lib/archiving/cache)" \
        "$(get_conf "$config_path" archived.cache_dir.min_free_space_KB 0 500000)"
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

# if pidfile exists and the pid is alive
if [ -e "$pidfile" ] && kill -0 "$(<"$pidfile")" &>/dev/null; then
    log err "Already running"
    exit 1
fi

# create the pidfile
echo -n $$ > "$pidfile"

log notice "Watchdog started"
while ! "${terminate_requested:-false}"; do
    if ! everything_ok; then
        log err "Check(s) failed; terminating so that the ArchiveD is stopped"
        exit 1;
    fi

    # is first check? To avoid warning about things that can't change once running
    first=false

    sleep 1
done
log notice "Watchdog stopped"

exit 0
