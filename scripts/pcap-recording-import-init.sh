#!/bin/bash
#
# rmarchpcapimport This shell script takes care of starting and
#                  stopping Archive PCAP Recording Import
#
# chkconfig: 2345 80 05
# description: Archive PCAP Recording Import
# processname: archiving-pcap-recording-import
# config: /etc/archiving/pcap-recording-import.conf
# pidfile: /var/run/archiving/rmarchpcapimport.pid

prog_longname="Archive PCAP Recording Import"
prog_shortname=rmarchpcapimport
prog_projprefix=archiving
prog_cliopts=""
prog_syslogtag=rm-archpcapimport

prog_binpath=/usr/bin/archiving-pcap-recording-import
prog_configpath=/etc/archiving/pcap-recording-import.conf
prog_pidfile=/var/run/${prog_projprefix}/${prog_shortname}.pid

. /usr/share/rmdaemonscripts/init.sh

