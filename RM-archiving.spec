%define version 0.0.1
%define appname archiving
%define buildtype dbgrel
%define __os_install_post %{nil}

Group: RedMatter/Archiving
Name: RM-%{appname}
Prefix: /
Release: 0
Source: RM-%{appname}-%{version}.tar.gz
URL: http://www.redmatter.com/
Version: %{version}
BuildRoot: /tmp/RM-%{appname}-%{version}
Summary: Red Matter %{appname}
License: Closed
Requires: boost >= 1.49.0
Requires: mysql++ >= 3.1.0-14089
Requires: cryptopp
Requires: libs3
Requires: RM-%{appname}-common = %{version}-%{release}
Requires: fcgi
Requires: httpd >= 2.4
Requires: RM-daemonscripts >= 0.0.1-22215
BuildRequires: boost-devel >= 1.49.0
BuildRequires: mysql++-devel >= 3.1.0-14089
BuildRequires: RM-librml-devel >= 0.0.1-21435
BuildRequires: libs3-devel >= 1.4-17269
BuildRequires: cryptopp-devel
BuildRequires: pugixml
BuildRequires: cpp-netlib >= 0.9.1-20788
BuildRequires: fcgi-devel
BuildRequires: rsync >= 3.0.8
BuildRequires: threadpool

%package collectd
Summary: Archiving CollectD
Group: RedMatter/Archiving
Requires: boost >= 1.45.0
Requires: mysql++ >= 3.1.0-14089
Requires: cryptopp
Requires: RM-daemonscripts >= 0.0.1-22215
Requires: RM-%{appname}-common = %{version}-%{release}
Requires: RM-fshooks >= 0.0.1-15219
Provides: cdrpostd
Conflicts: RM-cdrmunch-cdrpostd

%package collect-smsd
Summary: Archiving CollectSmsD
Group: RedMatter/Archiving
Requires: boost >= 1.45.0
Requires: mysql++ >= 3.1.0-14089
Requires: cryptopp
Requires: RM-daemonscripts >= 0.0.1-22215
Requires: RM-%{appname}-common = %{version}-%{release}
Provides: cdrpostd
Conflicts: RM-cdrmunch-cdrpostd

# %package migrationd
# Summary: Archiving CollectD
# Group: RedMatter/Archiving
# Requires: RM-%{appname}-common >= %{version}-%{release}
# Requires: boost >= 1.45.0
# Requires: mysql++ >= 3.1.0-14089
# Requires: libs3 = 1.4-17269
# Requires: cryptopp
# Requires: php-s3 >= 0.4.0-20785
# Requires: RM-php-rmapp >= 0.0.1-11853
# Requires: RM-%{appname}-convert = %{version}-%{release}

%package convert
Summary: Archiving Convert Utility
Group: RedMatter/Archiving
Requires: boost >= 1.45.0, cryptopp

%package common
Summary: Archiving Common files
Group: RedMatter/Archiving

%package scripts
Summary: Archiving Utility Scripts
Group: RedMatter/Archiving
Requires: RM-php-rmapp >= 0.0.1-22590
Requires: RM-php-eventlog
Requires: php-s3 >= 0.4.0-20785
Requires: RM-%{appname}-convert >= %{version}-%{release}

%package qascripts
Summary: Archiving Utility Scripts for QA
Group: RedMatter/Archiving
Requires: RM-%{appname}-scripts >= %{version}-%{release}
Requires: RM-syslogger >= 0.0.1-22449

%package gateway
Summary: Archiving / Storage gateway
Group: RedMatter/Archiving
Requires: RM-php-fluid >= 0.0.1-19760

# ------------ description---------------

%description
Archiving daemon ( ArchiveD )

%description collectd
Works on CDR from FreeSWITCH to identify files to be archived.
It also makes sense of archiving policy and passes that on to ArchiveD.

%description collect-smsd
Daemon that archives queued SMS messages from BES server via the callcontrol API.
It also makes sense of archiving policy and passes that on to ArchiveD.

# %description migrationd
# Archiving MigrationD

%description convert
Archiving Convert Utility; used by core-api to fetch files from S3

%description common
Archiving common config files, etc.

%description scripts
Archiving Utility scripts for verification etc

%description qascripts
Archiving QA scripts for verification etc

%description gateway
Archiving or Storage gateway; provides S3 like RESTful API to any POSIX filesystem

# ----------

%prep
%setup -q

%build
echo %{version} | awk -F\. '{print "VERSION_MAJOR="$1"\nVERSION_MINOR="$2"\nVERSION_PATCH="$3"\n"}' > version_info
echo "BUILD_ID=%{release}" >> version_info
make TMP=tmp-%{buildtype}-build BUILD_MODE=`echo %{buildtype} | tr 'a-z' 'A-Z'`

%install
mkdir -p %{buildroot}/usr/bin/
mkdir -p %{buildroot}/etc/archiving/
mkdir -p %{buildroot}/etc/logrotate.d/
mkdir -p %{buildroot}/etc/rsyslog.d/
mkdir -p %{buildroot}/etc/rc.d/init.d/
mkdir -p %{buildroot}/usr/share/archiving
# mkdir -p %{buildroot}/usr/share/archiving/migration-scripts
mkdir -p %{buildroot}/var/lib/archiving
mkdir -p %{buildroot}/var/run/archiving
mkdir -p %{buildroot}/usr/share/php/archiving
mkdir -p %{buildroot}/usr/share/php/rmarchgateway
mkdir -p %{buildroot}/var/www/rmarchgateway

# ArchiveD
mkdir -p %{buildroot}/var/www/rmarchived
mkdir -p %{buildroot}/etc/httpd/conf.d
cp archived/apache.conf %{buildroot}/etc/httpd/conf.d/rmarchived.conf
cp archived/tmp-%{buildtype}-build/archived %{buildroot}/usr/bin/rmarchived.bin
cp archived-monitor/tmp-%{buildtype}-build/archived-monitor %{buildroot}/usr/bin/rmarchived-monitor
cp archived/rmarchived_safe %{buildroot}/usr/bin/
cp archived/rmarchived %{buildroot}/etc/rc.d/init.d/
cp archived/rmarchived.supp %{buildroot}/usr/share/archiving/
cp archived/rmarchived.conf %{buildroot}/etc/archiving/
cp archived/logrotate.conf %{buildroot}/etc/logrotate.d/rmarchived
cp archived/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmarchived.conf

# CollectD
cp collectd/tmp-%{buildtype}-build/collectd %{buildroot}/usr/bin/rmcollectd.bin
cp collectd/rmcollectd_safe %{buildroot}/usr/bin/
cp collectd/rmcollectd %{buildroot}/etc/rc.d/init.d/
cp collectd/rmcollectd.supp %{buildroot}/usr/share/archiving/
cp collectd/rmcollectd.conf %{buildroot}/etc/archiving/
cp collectd/logrotate.conf %{buildroot}/etc/logrotate.d/rmcollectd
cp collectd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmcollectd.conf

# CollectSmsD
cp collect-smsd/tmp-%{buildtype}-build/collect-smsd %{buildroot}/usr/bin/rmcollect-smsd.bin
cp collect-smsd/rmcollect-smsd_safe %{buildroot}/usr/bin/
cp collect-smsd/rmcollect-smsd %{buildroot}/etc/rc.d/init.d/
cp collect-smsd/rmcollect-smsd.supp %{buildroot}/usr/share/archiving/
cp collect-smsd/rmcollect-smsd.conf %{buildroot}/etc/archiving/
cp collect-smsd/logrotate.conf %{buildroot}/etc/logrotate.d/rmcollect-smsd
cp collect-smsd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmcollect-smsd.conf

# # MigrationD
# cp migrationd/tmp-%{buildtype}-build/migrationd %{buildroot}/usr/bin/rmmigrationd.bin
# cp migrationd/rmmigrationd_safe %{buildroot}/usr/bin/
# cp migrationd/rmmigrationd %{buildroot}/etc/rc.d/init.d/
# cp migrationd/rmmigrationd.supp %{buildroot}/usr/share/archiving/
# cp migrationd/scripts/* %{buildroot}/usr/share/archiving/migration-scripts/.
# cp migrationd/rmmigrationd.conf %{buildroot}/etc/archiving/
# cp migrationd/logrotate.conf %{buildroot}/etc/logrotate.d/rmmigrationd
# cp migrationd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmmigrationd.conf

# Convert
cp convert/tmp-%{buildtype}-build/convert %{buildroot}/usr/bin/archiving-convert

# Common
mkdir -p %{buildroot}/etc/archiving/conf.d/
cp common/internal_database.conf  %{buildroot}/etc/archiving/conf.d/internal_database.conf
cp common/retention_database.conf %{buildroot}/etc/archiving/conf.d/retention_database.conf
cp common/cdrmunch_database.conf %{buildroot}/etc/archiving/conf.d/cdrmunch_database.conf
cp common/rmarchived-hAPI %{buildroot}/usr/bin/
# cp common/migration_database.conf %{buildroot}/etc/archiving/conf.d/migration_database.conf

# Scripts
cp scripts/inc/*.php  %{buildroot}/usr/share/php/archiving/.
cp scripts/archiving.sh %{buildroot}/usr/bin/archiving
cp scripts/checker.php %{buildroot}/usr/bin/archiving-checker
cp scripts/checker-feed.sh %{buildroot}/usr/bin/archiving-checker-feed.sh
cp scripts/checker.conf %{buildroot}/usr/share/archiving/archiving-checker.conf.sample
cp scripts/archiving-checker-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-checker.conf
cp scripts/extract.php %{buildroot}/usr/bin/archiving-extract
cp scripts/extract.conf %{buildroot}/usr/share/archiving/archiving-extract.conf.sample
cp scripts/archiving-extract-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-extract.conf
cp scripts/headerspatch.php %{buildroot}/usr/bin/archiving-headerspatch
cp scripts/headerspatch.conf %{buildroot}/usr/share/archiving/archiving-headerspatch.conf.sample
cp scripts/archiving-headerspatch-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-headerspatch.conf
cp scripts/retention_ro.conf %{buildroot}/usr/share/archiving/scripts-retention_ro.conf.sample
cp scripts/retention_rw.conf %{buildroot}/usr/share/archiving/scripts-retention_rw.conf.sample

# QA Scripts
cp scripts/qa/makepurgedata.php %{buildroot}/usr/bin/archiving-makepurgedata
cp scripts/qa/makepurgedata.conf %{buildroot}/usr/share/archiving/archiving-makepurgedata.conf.sample
cp scripts/qa/archiving-makepurgedata-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-makepurgedata.conf


# Gateway
rsync -az --exclude=*/test/* ./gateway/inc/ %{buildroot}/usr/share/php/rmarchgateway
cp gateway/index.php %{buildroot}/var/www/rmarchgateway/.
cp gateway/xhprof_pre.php %{buildroot}/var/www/rmarchgateway/.
cp gateway/gateway.conf %{buildroot}/etc/archiving/.
cp gateway/apache.conf %{buildroot}/etc/httpd/conf.d/rmarchgateway.conf
cp gateway/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmarchgateway.conf

# ----------

%pre
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 2051 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 2051 cdrmunch || exit
fi

%pre collectd
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 2051 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 2051 cdrmunch || exit
fi

%pre collect-smsd
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 2051 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 2051 cdrmunch || exit
fi

# %pre migrationd
# if grep ^cdrmunch: /etc/group &>/dev/null; then
# 	: # group already exists
# else
# 	/usr/sbin/groupadd -g 2051 cdrmunch || exit
# fi
#
# if ! /usr/bin/id cdrmunch &>/dev/null; then
# 	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 2051 cdrmunch || exit
# fi

#%pre common

# ----------

%post
/usr/bin/rmarchived-hAPI
/sbin/chkconfig --add rmarchived
/sbin/chkconfig rmarchived on
/sbin/chkconfig httpd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi

%post collectd
/usr/bin/rmarchived-hAPI
/sbin/chkconfig --add rmcollectd
/sbin/chkconfig rmcollectd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi

%post collect-smsd
/usr/bin/rmarchived-hAPI
/sbin/chkconfig --add rmcollect-smsd
/sbin/chkconfig rmcollect-smsd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi

#%post convert
#/usr/bin/rmarchived-hAPI

# %post migrationd
# /sbin/chkconfig --add rmmigrationd
# /sbin/chkconfig rmmigrationd on
# if [ "$1" -eq 1 ]; then # package just got installed ?
# 	[ -x /etc/init.d/rsyslog ] &&
# 		/etc/init.d/rsyslog condrestart ||
# 		echo "**WARNING** rsyslog not found; consider installing it."
# fi

%post scripts
/etc/init.d/rsyslog condrestart ||
	echo "**WARNING** rsyslog not found; consider installing it."

%post gateway
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi

%post qascripts
/etc/init.d/rsyslog condrestart ||
	echo "**WARNING** rsyslog not found; consider installing it."

# ----------

%postun
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi

%postun collectd
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi

%postun collect-smsd
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi

%postun gateway
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi

# ----------

%files
%defattr(-,root,root)
/usr/bin/rmarchived.bin
/usr/bin/rmarchived-monitor
/usr/bin/rmarchived_safe
/usr/share/archiving/rmarchived.supp
/etc/rc.d/init.d/rmarchived
/etc/archiving/rmarchived.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmarchived
/etc/rsyslog.d/11-rmarchived.conf
/etc/archiving/conf.d/retention_database.conf
/etc/archiving/conf.d/cdrmunch_database.conf
/var/www/rmarchived
/etc/httpd/conf.d/rmarchived.conf

%files collectd
%defattr(-,root,root)
/usr/bin/rmcollectd.bin
/usr/bin/rmcollectd_safe
/usr/share/archiving/rmcollectd.supp
/etc/rc.d/init.d/rmcollectd
/etc/archiving/rmcollectd.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmcollectd
/etc/rsyslog.d/11-rmcollectd.conf

%files collect-smsd
%defattr(-,root,root)
/usr/bin/rmcollect-smsd.bin
/usr/bin/rmcollect-smsd_safe
/usr/share/archiving/rmcollect-smsd.supp
/etc/rc.d/init.d/rmcollect-smsd
/etc/archiving/rmcollect-smsd.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmcollect-smsd
/etc/rsyslog.d/11-rmcollect-smsd.conf

# %files migrationd
# %defattr(-,root,root)
# /usr/bin/rmmigrationd.bin
# /usr/bin/rmmigrationd_safe
# /usr/share/archiving/rmmigrationd.supp
# /etc/rc.d/init.d/rmmigrationd
# %config(noreplace) /etc/archiving/rmmigrationd.conf
# %dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
# %dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
# /etc/logrotate.d/rmmigrationd
# /etc/rsyslog.d/11-rmmigrationd.conf
# %dir /usr/share/archiving/migration-scripts
# /usr/share/archiving/migration-scripts/*

%files convert
%defattr(-,root,root)
/usr/bin/archiving-convert

%files common
%attr(755,root,root) /usr/bin/rmarchived-hAPI
%dir /etc/archiving/conf.d/
/etc/archiving/conf.d/internal_database.conf
# /etc/archiving/conf.d/migration_database.conf

%files scripts
%defattr(-,root,root)
/usr/share/php/archiving/class.ArchiveD.php
/usr/share/php/archiving/class.Archiving.php
/usr/share/php/archiving/class.ArchivingChecker.php
/usr/share/php/archiving/class.ArchivingEndpoint.php
/usr/share/php/archiving/class.ArchivingRetention.php
/usr/share/php/archiving/class.ArchivingPolicy.php
/usr/share/php/archiving/class.HTTPStream.php
/usr/bin/archiving
/usr/bin/archiving-checker
/usr/bin/archiving-checker-feed.sh
/usr/bin/archiving-extract
/usr/bin/archiving-headerspatch
/usr/share/archiving/archiving-checker.conf.sample
/usr/share/archiving/archiving-extract.conf.sample
/usr/share/archiving/archiving-headerspatch.conf.sample
/usr/share/archiving/scripts-retention_ro.conf.sample
/usr/share/archiving/scripts-retention_rw.conf.sample
/etc/rsyslog.d/11-archiving-checker.conf
/etc/rsyslog.d/11-archiving-extract.conf
/etc/rsyslog.d/11-archiving-headerspatch.conf

%files qascripts
%defattr(-,root,root)
%attr(755,root,root) /usr/bin/archiving-makepurgedata
/usr/share/archiving/archiving-makepurgedata.conf.sample
/etc/rsyslog.d/11-archiving-makepurgedata.conf

%files gateway
%defattr(-,root,root)
/usr/share/php/rmarchgateway/*
/var/www/rmarchgateway/index.php
/var/www/rmarchgateway/xhprof_pre.php
/etc/archiving/gateway.conf
/etc/httpd/conf.d/rmarchgateway.conf
/etc/rsyslog.d/11-rmarchgateway.conf

# ----------

%clean
rm -fR %{buildroot}
