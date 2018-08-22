#!/bin/bash
#
# rmestuary   This shell script takes care of starting and stopping
#             Archiving Estuary.
#
# chkconfig: 2345 80 05
# description: Archiving Estuary
# processname: rmestuary
# config: /etc/archiving/rmestuary.conf
# pidfile: /var/run/archiving/rmestuary.pid

prog_longname="Archiving Estuary"
prog_shortname=rmestuary
prog_projprefix=archiving
prog_cliopts=""
prog_syslogtag=rm-estuary

. /usr/share/rmdaemonscripts/init.sh
