#!/bin/bash
#
# rmarchdsrpreproc This shell script takes care of starting and
#                  stopping Archiving DSR PreProcessor
#
# chkconfig: 2345 80 05
# description: Archiving DSR PreProcessor
# processname: archiving-dsr-pre-processor
# config: /etc/archiving/dsr-preproc.conf
# pidfile: /var/run/archiving/rmarchdsrpreproc.pid

prog_longname="Archiving DSR PreProcessor"
prog_shortname=rmarchdsrpreproc
prog_projprefix=archiving
prog_cliopts=""
prog_syslogtag=rm-archdsrpreproc

prog_binpath=/usr/bin/archiving-dsr-pre-processor
prog_configpath=/etc/archiving/dsr-preproc.conf
prog_pidfile=/var/run/${prog_projprefix}/${prog_shortname}.pid

. /usr/share/rmdaemonscripts/init.sh

