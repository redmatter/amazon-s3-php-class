#!/bin/bash
#
# rmarchpolfix   This shell script takes care of starting and stopping
#                the Archive Purge Policy Fixer Daemon.
#
# chkconfig: 2345 80 05
# description: Archive Purge Policy Fixer Daemon
# processname: rmarchpolfix
# config: /etc/archiving/purge-policy-fixer.conf
# pidfile: /var/run/archiving/rmarchpolfix.pid

prog_longname="Archive Purge Policy Fixer Daemon"
prog_shortname=rmarchpolfix
prog_projprefix=archiving
prog_cliopts=""
prog_syslogtag=rm-archpolfix

prog_binpath=/usr/bin/archiving-purge-policy-fixer
prog_configpath=/etc/archiving/purge-policy-fixer.conf
prog_pidfile=/var/run/${prog_projprefix}/${prog_shortname}.pid

. /usr/share/rmdaemonscripts/init.sh

