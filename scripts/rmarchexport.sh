#!/bin/bash
#
# rmarchexport   This shell script takes care of starting and stopping
#                the Archive Export Daemon.
#
# chkconfig: 2345 80 05
# description: Archive Export Daemon
# processname: rmarchexport
# config: /etc/archiving/export.conf
# pidfile: /var/run/archiving/rmarchexport.pid

prog_longname="Archive Export Daemon"
prog_shortname=rmarchexport
prog_projprefix=archiving
prog_cliopts=""
prog_syslogtag=rm-archexport

prog_binpath=/usr/bin/archiving-export
prog_configpath=/etc/archiving/export.conf
prog_pidfile=/var/run/${prog_projprefix}/${prog_shortname}.pid

. /usr/share/rmdaemonscripts/init.sh

