%{!?__build_pkg_version: %define __build_pkg_version 1.0.0}
%{!?__build_pkg_release: %define __build_pkg_release 1}
%{!?__librml_buildtype: %define __librml_buildtype DBGREL}

%define appname archiving
%define buildtype %{__librml_buildtype}
%define __os_install_post %{nil}

Group: RedMatter/Archiving
Name: RM-%{appname}
Prefix: /
Version: %{__build_pkg_version}
Source: RM-%{appname}-%{version}.tar.gz
URL: http://www.redmatter.com/
Release: %{__build_pkg_release}%{?dist}
BuildRoot: /tmp/RM-%{appname}-%{version}
Summary: Red Matter %{appname}
License: Closed
Requires: mysql++ >= 3.1.0-29693
%{?el7:Requires: mariadb-libs}
Requires: cryptopp
%{?el5:Requires: libs3 >= 1.4-22774}
%{?el7:Requires: libs3 = 1.4-26468.1.0.0.dc7}
Requires: RM-%{appname}-common = %{version}-%{release}
Requires: RM-%{appname}-platform-config = %{version}-%{release}
Requires: fcgi
Requires: httpd >= 2.4
Requires: RM-daemonscripts >= 1.1.0
%if 0%{?el5}
BuildRequires: mysql++-devel >= 3.1.0-29693
BuildRequires: php54-cli
BuildRequires: pugixml
%else
%if 0%{?el7}
BuildRequires: mysql++-devel >= 3.1.0
BuildRequires: mariadb-devel
BuildRequires: RM-legacy-php54-cli >= 1.0.1
BuildRequires: pugixml pugixml-devel
%endif
%endif
BuildRequires: gcc
BuildRequires: gcc-c++
BuildRequires: boost_1_54_0-devel
BuildRequires: RM-librml-devel = 1.10.0
%{?el5:BuildRequires: libs3-devel >= 1.4-22774}
%{?el7:BuildRequires: libs3-devel = 1.4-26468.1.0.0.dc7}
BuildRequires: cryptopp-devel
BuildRequires: fcgi-devel
BuildRequires: rsync >= 3.0.8
BuildRequires: threadpool
BuildRequires: cmake
BuildRequires: openssl-devel
BuildRequires: zlib-devel
%if 0%{?el7}
BuildRequires: libuuid-devel
%endif
BuildRequires: autoconf, libtool

%package platform-config-redmatter
Summary: ArchiveD config specific to redmatter platform
Group: RedMatter/Archiving
Provides: RM-%{appname}-platform-config = %{version}-%{release}
Conflicts: RM-%{appname}-platform-config-ipc

%package platform-config-ipc
Summary: ArchiveD config specific to redmatter platform
Group: RedMatter/Archiving
Provides: RM-%{appname}-platform-config = %{version}-%{release}
Conflicts: RM-%{appname}-platform-config-redmatter

%package collectd
Summary: Archiving CollectD
Group: RedMatter/Archiving
Requires: mysql++ >= 3.1.0-29693
%{?el7:Requires: mariadb-libs}
Requires: cryptopp
Requires: RM-daemonscripts >= 1.1.0
Requires: RM-%{appname}-common = %{version}-%{release}
Requires: RM-fscore >= 0.0.1-26178
Provides: cdrpostd
Conflicts: RM-cdrmunch-cdrpostd

%package collectd-schema
Summary: Archiving CollectD Schema
Group: RedMatter/Archiving
Requires: RM-%{appname}-collectd = %{version}-%{release}

%package collect-smsd
Summary: Archiving CollectSmsD
Group: RedMatter/Archiving
Requires: mysql++ >= 3.1.0-29693
%{?el7:Requires: mariadb-libs}
Requires: cryptopp
Requires: RM-daemonscripts >= 1.1.0
Requires: RM-%{appname}-common = %{version}-%{release}

%package collect-importd
Summary: Archiving CollectImportD
Group: RedMatter/Archiving
Requires: mysql++ >= 3.1.0-29693
%{?el7:Requires: mariadb-libs}
Requires: cryptopp
Requires: RM-daemonscripts >= 1.1.0
Requires: RM-%{appname}-common = %{version}-%{release}
BuildRequires: php-cli
%if 0%{?el5}
BuildRequires: php54-cli
%else
%if 0%{?el7}
BuildRequires: RM-legacy-php54-cli >= 1.0.1
%endif
%endif

# %package migrationd
# Summary: Archiving CollectD
# Group: RedMatter/Archiving
# Requires: RM-%{appname}-common >= %{version}-%{release}
# Requires: mysql++ >= 3.1.0-29693
# %{?el7:Requires: mariadb-libs}
# Requires: libs3 = 1.4-17269
# Requires: cryptopp
# Requires: php-s3 >= 0.4.0-20785
# Requires: RM-php-rmapp >= 0.0.5
# Requires: RM-%{appname}-convert = %{version}-%{release}

%package convert
Summary: Archiving Convert Utility
Group: RedMatter/Archiving
Requires: cryptopp

%package cli
Summary: Archiving CLI Tools
Group: RedMatter/Archiving
Requires: cryptopp

%package common
Summary: Archiving Common files
Group: RedMatter/Archiving

%package estuary
Summary: Archiving Estuary
Group: RedMatter/Archiving
Requires: mysql++ >= 3.1.0-29693
%{?el7:Requires: mariadb-libs}
Requires: cryptopp
Requires: RM-daemonscripts >= 1.1.0
Requires: RM-%{appname}-common = %{version}-%{release}

%package scripts
Summary: Archiving Utility Scripts
Group: RedMatter/Archiving
Requires: RM-php-rmapp >= 0.0.5
Requires: RM-php-eventlog
Requires: php-s3 >= 0.4.0-41402
Requires: RM-%{appname}-convert >= %{version}-%{release}
Requires: RM-daemonscripts >= 1.1.0
Requires: p7zip
Requires: RM-pcaptools >= 0.0.1-42064
Requires: RM-%{appname}-cli >= %{version}-%{release}

%package qascripts
Summary: Archiving Utility Scripts for QA
Group: RedMatter/Archiving
#Requires: RM-%{appname}-scripts >= %{version}-%{release}
Requires: RM-%{appname}-scripts
Requires: RM-%{appname}-purge-verify
Requires: RM-syslogger >= 0.0.1-22449
Requires: php-gd
Requires: sox

%package purge-verify
Summary: Archiving Utility Scripts for Purge Verification
Group: RedMatter/Archiving
%if 0%{?el5}
Requires: php54-cli
%else
%if 0%{?el7}
Requires: RM-legacy-php54-cli >= 1.0.1
%endif
%endif
Requires: RM-%{appname}-scripts
Requires: RM-syslogger >= 0.0.1-22449

%package eml2a
Summary: Tools to puush SMS and MMS email messages into archive
Group: RedMatter/Archiving
Requires: postfix
Requires: RM-syslogger >= 0.0.1-22449
Requires: RM-daemonscripts >= 1.1.0
BuildRequires: sendmail-devel
%if 0%{?el5}
BuildRequires: pugixml
BuildRequires: db4-devel
%else
%if 0%{?el7}
BuildRequires: pugixml pugixml-devel
BuildRequires: libdb-devel
%endif
%endif

# ------------ description---------------

%description
Archiving daemon ( ArchiveD )

%description platform-config-redmatter
ArchiveD platform configs for redmatter

%description platform-config-ipc
ArchiveD platform configs for IPC (Etrali)

%description collectd
Works on CDR from FreeSWITCH to identify files to be archived.
It also makes sense of archiving policy and passes that on to ArchiveD.

%description collectd-schema
CollectD Schema files

%description collect-smsd
Daemon that archives queued SMS messages from BES server via the callcontrol API.
It also makes sense of archiving policy and passes that on to ArchiveD.

%description collect-importd
Daemon that archives audio recordings imported from elsewhere, e.g. Telia Phonero.
It also makes sense of archiving policy and passes that on to ArchiveD.

# %description migrationd
# Archiving MigrationD

%description convert
Archiving Convert Utility; used by core-api to fetch files from S3

%description cli
Archiving CLI Tools; used by various scripts

%description common
Archiving common config files, etc.

%description estuary
HTTP PUT interface to ArchiveD for adding objects

%description scripts
Archiving Utility scripts for verification etc

%description qascripts
Archiving QA scripts for verification etc

%description purge-verify
Archiving Purge Verification scripts

%description eml2a
A milter that attaches to postscript, and processes messages from recognised
email addresses and pushes them into the archive using a one-shot archiving
tool.

# ----------

%prep
%setup -q

%build
echo %{version} | awk -F\. '{print "VERSION_MAJOR="$1"\nVERSION_MINOR="$2"\nVERSION_PATCH="$3"\n"}' > version_info
echo "BUILD_ID=%{release}" >> version_info
# For CentOS 7, if pugixml header is not located at /usr/include/pugixml/pugixml.hpp then create symlink to actual header
%if 0%{?el5}
CC=gcc CXX=g++ make TMP=tmp-%{buildtype}-build BUILD_MODE=%{buildtype}
%else
%if 0%{?el7}
export PKG_CONFIG_PATH=/usr/lib64/mysql:$PKG_CONFIG_PATH
if [ -f /usr/include/pugixml/pugixml.hpp ]
then
echo Found pugixml header at expected location
CC=gcc CXX=g++ make TMP=tmp-%{buildtype}-build BUILD_MODE=%{buildtype}
else
echo Did not find pugixml header at expected location, creating symlink
mkdir /usr/include/pugixml
ln -s /usr/include/pugixml.hpp /usr/include/pugixml/pugixml.hpp
CC=gcc CXX=g++ make TMP=tmp-%{buildtype}-build BUILD_MODE=%{buildtype}
rm -rf /usr/include/pugixml
fi
%endif
%endif

%install
mkdir -p %{buildroot}/usr/bin/
mkdir -p %{buildroot}/etc/archiving/conf.d/
mkdir -p %{buildroot}/etc/logrotate.d/
mkdir -p %{buildroot}/etc/rsyslog.d/
%if 0%{?el5}
mkdir -p %{buildroot}/etc/rc.d/init.d/
%endif
%if 0%{?el7}
mkdir -p %{buildroot}%{_unitdir}
%endif
mkdir -p %{buildroot}/usr/share/archiving/bin
# mkdir -p %{buildroot}/usr/share/archiving/migration-scripts
mkdir -p %{buildroot}/var/lib/archiving
mkdir -p %{buildroot}/var/run/archiving
mkdir -p %{buildroot}/usr/share/php/archiving

# ArchiveD
mkdir -p %{buildroot}/var/www/rmarchived
mkdir -p %{buildroot}/etc/httpd/conf.d
cp archived/apache.conf %{buildroot}/etc/httpd/conf.d/rmarchived.conf
cp archived/tmp-%{buildtype}-build/archived %{buildroot}/usr/bin/rmarchived.bin
cp archived-monitor/tmp-%{buildtype}-build/archived-monitor %{buildroot}/usr/bin/rmarchived-monitor
cp archived/rmarchived_safe %{buildroot}/usr/bin/
%if 0%{?el5}
cp archived/rmarchived %{buildroot}/etc/rc.d/init.d/
%endif
%if 0%{?el7}
install -Dpm 0644 archived/rmarchived.service %{buildroot}%{_unitdir}
%endif
cp archived/rmarchived.supp %{buildroot}/usr/share/archiving/
cp archived/rmarchived.conf %{buildroot}/etc/archiving/
cp archived/logrotate.conf %{buildroot}/etc/logrotate.d/rmarchived
cp archived/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmarchived.conf

# ArchiveD retention-purge config (platform specific)
cp archived/retention-purge-config/redmatter.conf %{buildroot}/etc/archiving/conf.d/retention-purge-redmatter.conf
cp archived/retention-purge-config/ipc.conf %{buildroot}/etc/archiving/conf.d/retention-purge-ipc.conf

# CollectD
cp collectd/tmp-%{buildtype}-build/collectd %{buildroot}/usr/bin/rmcollectd.bin
cp collectd/rmcollectd_safe %{buildroot}/usr/bin/
%if 0%{?el5}
cp collectd/rmcollectd %{buildroot}/etc/rc.d/init.d/
%endif
%if 0%{?el7}
install -Dpm 0644 collectd/rmcollectd.service %{buildroot}%{_unitdir}
%endif
cp collectd/rmcollectd.supp %{buildroot}/usr/share/archiving/
cp collectd/rmcollectd.conf %{buildroot}/etc/archiving/
cp collectd/logrotate.conf %{buildroot}/etc/logrotate.d/rmcollectd
cp collectd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmcollectd.conf
cp collectd/sqlite.conf %{buildroot}/etc/archiving/conf.d/sqlite.conf
cp collectd/collectd.db3 %{buildroot}/usr/share/archiving/blank_collectd.db3
cp external/sqlite/bin/sqlite3 %{buildroot}/usr/share/archiving/bin/sqlite3

# CollectD Schema
# bash-pack under "collectd/schema/redstart/vendor" is brought in by a build step
# docker run --rm -it --volume="$(pwd)/collectd/schema/redstart:/project" redmatter/gitman install
cp -r collectd/schema %{buildroot}/usr/share/archiving/collectd-schema

# CollectSmsD
cp collect-smsd/tmp-%{buildtype}-build/collect-smsd %{buildroot}/usr/bin/rmcollect-smsd.bin
cp collect-smsd/rmcollect-smsd_safe %{buildroot}/usr/bin/
%if 0%{?el5}
cp collect-smsd/rmcollect-smsd %{buildroot}/etc/rc.d/init.d/
%endif
%if 0%{?el7}
install -Dpm 0644 collect-smsd/rmcollect-smsd.service %{buildroot}%{_unitdir}
%endif
cp collect-smsd/rmcollect-smsd.supp %{buildroot}/usr/share/archiving/
cp collect-smsd/rmcollect-smsd.conf %{buildroot}/etc/archiving/
cp collect-smsd/logrotate.conf %{buildroot}/etc/logrotate.d/rmcollect-smsd
cp collect-smsd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmcollect-smsd.conf

# CollectImportD
cp collect-importd/tmp-%{buildtype}-build/collect-importd %{buildroot}/usr/bin/rmcollect-importd.bin
cp collect-importd/rmcollect-importd_safe %{buildroot}/usr/bin/
%if 0%{?el5}
cp collect-importd/rmcollect-importd %{buildroot}/etc/rc.d/init.d/
%endif
%if 0%{?el7}
install -Dpm 0644 collect-importd/rmcollect-importd.service %{buildroot}%{_unitdir}
%endif
cp collect-importd/rmcollect-importd.supp %{buildroot}/usr/share/archiving/
cp collect-importd/rmcollect-importd.conf %{buildroot}/etc/archiving/
cp collect-importd/logrotate.conf %{buildroot}/etc/logrotate.d/rmcollect-importd
cp collect-importd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmcollect-importd.conf
mkdir -p %{buildroot}/mnt/rmfs/archiving-import
mkdir -p %{buildroot}/mnt/rmfs/phonero/download
mkdir -p %{buildroot}/mnt/rmfs/phonero/queue
mkdir -p %{buildroot}/mnt/rmfs/phonero/not-handled
cp collect-importd/cronjobs/phonero/archiving-phonero-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-phonero-cron.conf
cp collect-importd/cronjobs/phonero/phonero-cron.conf %{buildroot}/etc/archiving/phonero-cron.conf
cp collect-importd/cronjobs/phonero/phonero-cron.phar %{buildroot}/usr/bin/
mkdir -p %{buildroot}/etc/cron.1min
ln -sf /usr/bin/phonero-cron.phar %{buildroot}/etc/cron.1min

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

# CLI
for bin in convert-to-clear convert-to-archive download; do
	cp cli/$bin/tmp-%{buildtype}-build/$bin %{buildroot}/usr/bin/archiving-cli-$bin
done
cp cli/cli.sh %{buildroot}/usr/bin/archiving-cli

# Common
cp common/internal_database.conf  %{buildroot}/etc/archiving/conf.d/internal_database.conf
cp common/retention_database.conf %{buildroot}/etc/archiving/conf.d/retention_database.conf
cp common/retention_delete_database.conf %{buildroot}/etc/archiving/conf.d/retention_delete_database.conf
cp common/cdrmunch_database.conf %{buildroot}/etc/archiving/conf.d/cdrmunch_database.conf
cp common/rmarchived-hAPI %{buildroot}/usr/bin/

# Estuary
cp estuary/tmp-%{buildtype}-build/rmestuary %{buildroot}/usr/bin/rmestuary.bin
%if 0%{?el5}
cp estuary/rmestuary.sh %{buildroot}/etc/rc.d/init.d/rmestuary
%endif
%if 0%{?el7}
install -Dpm 0644 estuary/rmestuary.service %{buildroot}%{_unitdir}
%endif
cp estuary/rmestuary.conf %{buildroot}/etc/archiving/
cp estuary/logrotate.conf %{buildroot}/etc/logrotate.d/rmestuary
cp estuary/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmestuary.conf

# Scripts
for file in $( find scripts/inc -type f -not \( -path "scripts/inc/JsonSchema/*" -o -path "scripts/inc/JsonMapper/*" \) ); do
	install -m 644 -D ${file} %{buildroot}/usr/share/php/archiving/${file#scripts/inc/}
done
ln -sf /usr/share/php/archiving/aws-2.7.17.phar %{buildroot}/usr/share/php/archiving/aws.phar
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
cp scripts/archiving-headerspatch-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-headerspatch
cp scripts/purge-policy-fixer.php %{buildroot}/usr/bin/archiving-purge-policy-fixer
cp scripts/purge-policy-fixer.conf %{buildroot}/etc/archiving/purge-policy-fixer.conf
%if 0%{?el5}
cp scripts/rmarchpolfix.sh %{buildroot}/etc/rc.d/init.d/rmarchpolfix
%endif
%if 0%{?el7}
install -Dpm 0644 scripts/rmarchpolfix.service %{buildroot}%{_unitdir}
%endif
cp scripts/archiving-purge-policy-fixer-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-purge-policy-fixer.conf
cp scripts/archiving-purge-policy-fixer-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-purge-policy-fixer
cp scripts/export.php %{buildroot}/usr/bin/archiving-export
cp scripts/export.conf %{buildroot}/etc/archiving/export.conf
%if 0%{?el5}
cp scripts/rmarchexport.sh %{buildroot}/etc/rc.d/init.d/rmarchexport
%endif
%if 0%{?el7}
install -Dpm 0644 scripts/rmarchexport.service %{buildroot}%{_unitdir}
%endif
cp scripts/archiving-export-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-export.conf
cp scripts/archiving-export-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-export
cp scripts/retention_ro.conf %{buildroot}/etc/archiving/scripts-retention_ro.conf
cp scripts/retention_rw.conf %{buildroot}/etc/archiving/scripts-retention_rw.conf
# pcap-import
mkdir -p %{buildroot}/var/lib/archiving/pcap-import/done
cp scripts/pcap-recording-import.php %{buildroot}/usr/bin/archiving-pcap-recording-import
cp scripts/pcap-recording-import.conf %{buildroot}/etc/archiving/pcap-recording-import.conf
%if 0%{?el5}
cp scripts/pcap-recording-import-init.sh %{buildroot}/etc/rc.d/init.d/rmarchpcapimport
%endif
%if 0%{?el7}
install -Dpm 0644 scripts/rmarchpcapimport.service %{buildroot}%{_unitdir}
%endif
cp scripts/pcap-recording-import-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-pcap-recording-import.conf
cp scripts/pcap-recording-import-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-pcap-recording-import
cp common/big_database.conf %{buildroot}/etc/archiving/conf.d/big_database.conf
# dsr-pre-processor
cp scripts/dsr-pre-processor/app.php %{buildroot}/usr/bin/archiving-dsr-pre-processor
cp scripts/dsr-pre-processor/app.conf %{buildroot}/etc/archiving/dsr-preproc.conf
cp scripts/dsr-pre-processor/autoload.php %{buildroot}/usr/share/php/archiving/archdsrpreproc_autoload.php
cp scripts/dsr-pre-processor/init.sh %{buildroot}/etc/rc.d/init.d/rmarchdsrpreproc
cp scripts/dsr-pre-processor/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-dsr-pre-processor.conf
cp scripts/dsr-pre-processor/logrotate.conf %{buildroot}/etc/logrotate.d/archiving-dsr-pre-processor

# qascripts
cp scripts/qa/makepurgedata.php %{buildroot}/usr/bin/archiving-makepurgedata
cp scripts/qa/makepurgedata.conf %{buildroot}/usr/share/archiving/archiving-makepurgedata.conf.sample
cp scripts/qa/archiving-makepurgedata-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-makepurgedata.conf
cp scripts/qa/archiving-makepurgedata-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-makepurgedata
cp scripts/qa/send-test-eml.php %{buildroot}/usr/bin
cp -r collectd/tests/cdrgen %{buildroot}/usr/share/archiving/collectd-cdrgen

# purge-verify
cp scripts/qa/verifypurge.php %{buildroot}/usr/bin/archiving-verifypurge
cp scripts/qa/verifypurge.conf %{buildroot}/usr/share/archiving/archiving-verifypurge.conf.sample
cp scripts/qa/archiving-verifypurge-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-verifypurge.conf
cp scripts/qa/archiving-verifypurge-logrotate.conf %{buildroot}/etc/logrotate.d/archiving-verifypurge
cp scripts/purge/make-verification-csv/app.php %{buildroot}/usr/bin/archiving-make-purge-verification-csv
cp scripts/purge/make-verification-csv/app.conf %{buildroot}/etc/archiving/make-purge-verification-csv.conf
cp scripts/purge/make-verification-csv/definition-schema.json %{buildroot}/usr/share/archiving/purge-verify-definition-schema.json
cp scripts/purge/make-verification-csv/example-definition.json %{buildroot}/usr/share/archiving/purge-verify-definition.example.json
cp scripts/purge/make-verification-csv/autoload.php %{buildroot}/usr/share/php/archiving/purgeverifycsv_autoload.php

# Other scripts
cp scripts/archiving-sms-recovery.php %{buildroot}/usr/bin/archiving-sms-recovery
cp scripts/archiving-sms-recovery.conf %{buildroot}/usr/share/archiving/archiving-sms-recovery.conf.sample
cp scripts/archiving-sms-recovery-rsyslog.conf %{buildroot}/etc/rsyslog.d/11-archiving-sms-recovery.conf

# eml2a
mkdir -p %{buildroot}/var/lib/eml2a/messages
mkdir -p %{buildroot}/var/run/eml2a
mkdir -p %{buildroot}/usr/share/eml2a
cp eml2a/tmp-%{buildtype}-build/eml2a %{buildroot}/usr/bin
cp eml2a/eml2a-milter %{buildroot}/usr/bin
cp eml2a/eml2a-milter_safe %{buildroot}/usr/bin/
%if 0%{?el5}
cp eml2a/eml2a-milter.init %{buildroot}/etc/rc.d/init.d/eml2a-milter
%endif
%if 0%{?el7}
install -Dpm 0644 eml2a/eml2a-milter.service %{buildroot}%{_unitdir}
%endif
cp eml2a/eml2a.conf %{buildroot}/etc/archiving/
cp eml2a/eml2a-milter.conf %{buildroot}/usr/share/eml2a
cp eml2a/logrotate.conf %{buildroot}/etc/logrotate.d/eml2a
cp eml2a/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-eml2a.conf
cp eml2a/eml2a-milter-hAPI %{buildroot}/usr/bin

# ----------

%pre
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

%pre collectd
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

%pre collect-smsd
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

%pre collect-importd
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

# %pre migrationd
# if grep ^cdrmunch: /etc/group &>/dev/null; then
# 	: # group already exists
# else
# 	/usr/sbin/groupadd -g 134 cdrmunch || exit
# fi
#
# if ! /usr/bin/id cdrmunch &>/dev/null; then
# 	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
# fi

%pre estuary
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

%pre eml2a
if grep ^cdrmunch: /etc/group &>/dev/null; then
	: # group already exists
else
	/usr/sbin/groupadd -g 134 cdrmunch || exit
fi

if ! /usr/bin/id cdrmunch &>/dev/null; then
	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 134 cdrmunch || exit
fi

#%pre common

# ----------

%post
/usr/bin/rmarchived-hAPI &> /dev/null || /bin/true
%if 0%{?el5}
/sbin/chkconfig --add rmarchived
/sbin/chkconfig rmarchived on
/sbin/chkconfig httpd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable rmarchived
%endif

%post platform-config-redmatter
ln -sf /etc/archiving/conf.d/retention-purge-redmatter.conf /etc/archiving/rmarchived-retention-purge.conf

%post platform-config-ipc
ln -sf /etc/archiving/conf.d/retention-purge-ipc.conf /etc/archiving/rmarchived-retention-purge.conf

%post collectd
/usr/bin/rmarchived-hAPI &> /dev/null || /bin/true
%if 0%{?el5}
/sbin/chkconfig --add rmcollectd
/sbin/chkconfig rmcollectd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable rmcollectd
%endif
if [ ! -f /var/lib/archiving/collectd.db3 ]; then
	cp /usr/share/archiving/blank_collectd.db3 /var/lib/archiving/collectd.db3
	# TODO change permissions if we change the user we run collectd as
fi

%post collect-smsd
/usr/bin/rmarchived-hAPI &> /dev/null || /bin/true
%if 0%{?el5}
/sbin/chkconfig --add rmcollect-smsd
/sbin/chkconfig rmcollect-smsd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable rmcollect-smsd
%endif

%post collect-importd
/usr/bin/rmarchived-hAPI &> /dev/null || /bin/true
%if 0%{?el5}
/sbin/chkconfig --add rmcollect-importd
/sbin/chkconfig rmcollect-importd on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable rmcollect-importd
%endif

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

%post estuary
%if 0%{?el5}
/sbin/chkconfig --add rmestuary
/sbin/chkconfig rmestuary on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable rmestuary
%endif

%post scripts
%if 0%{?el5}
/sbin/chkconfig --add rmarchpolfix
/sbin/chkconfig rmarchpolfix on
/sbin/chkconfig --add rmarchexport
/sbin/chkconfig rmarchexport on
/sbin/chkconfig --add rmarchpcapimport
/sbin/chkconfig rmarchpcapimport on
/etc/init.d/rsyslog condrestart ||
	echo "**WARNING** rsyslog not found; consider installing it."
%endif
%if 0%{?el7}
systemctl enable rmarchpolfix
systemctl enable rmarchexport
systemctl enable rmarchpcapimport
%endif

/sbin/chkconfig --add rmarchdsrpreproc
/sbin/chkconfig rmarchdsrpreproc on
/etc/init.d/rsyslog condrestart ||
	echo "**WARNING** rsyslog not found; consider installing it."

%post qascripts
%if 0%{?el5}
/etc/init.d/rsyslog condrestart ||
	echo "**WARNING** rsyslog not found; consider installing it."
%endif

# %post qascripts

%post eml2a
%if 0%{?el5}
/sbin/chkconfig --add eml2a-milter
/sbin/chkconfig eml2a-milter on
if [ "$1" -eq 1 ]; then # package just got installed ?
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		echo "**WARNING** rsyslog not found; consider installing it."
fi
%endif
%if 0%{?el7}
systemctl enable eml2a-milter
%endif
/usr/sbin/postconf -e "smtpd_milters = unix:/var/lib/eml2a/eml2a-milter.sock"
/usr/sbin/postconf -e "milter_default_action = tempfail"
%if 0%{?el5}
# note: this will do nothing if postfix is not running
/sbin/service postfix condrestart &> /dev/null
%endif

# ----------

%postun
%if 0%{?el5}
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi
%endif

%postun platform-config-redmatter
%if 0%{?el5}
if [ "$1" -eq 0 ]; then # package being uninstalled
	rm /etc/archiving/rmarchived-retention-purge.conf
fi
%endif

%postun platform-config-ipc
%if 0%{?el5}
if [ "$1" -eq 0 ]; then # package being uninstalled
	rm /etc/archiving/rmarchived-retention-purge.conf
fi
%endif

%postun collectd
%if 0%{?el5}
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi
%endif

%postun collect-smsd
%if 0%{?el5}
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi
%endif

%postun collect-importd
%if 0%{?el5}
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi
%endif

%postun eml2a
if [ "$1" -eq 0 ]; then
	# update main.cf with the DEFAULT values for the settings we changed
	# on installation
	/usr/sbin/postconf -e "`/usr/sbin/postconf -d smtpd_milters`"
	/usr/sbin/postconf -e "`/usr/sbin/postconf -d milter_default_action`"
%if 0%{?el5}
	# note: this will do nothing if postfix is not running
	/sbin/service postfix condrestart &> /dev/null
%endif
fi

%postun estuary
%if 0%{?el5}
if [ "$1" -eq 0 ]; then
	[ -x /etc/init.d/rsyslog ] &&
		/etc/init.d/rsyslog condrestart ||
		/bin/true
fi
%endif

# ----------

%files
%defattr(-,root,root)
/usr/bin/rmarchived.bin
/usr/bin/rmarchived-monitor
/usr/bin/rmarchived_safe
/usr/share/archiving/rmarchived.supp
%if 0%{?el5}
/etc/rc.d/init.d/rmarchived
%endif
%if 0%{?el7}
%{_unitdir}/rmarchived.service
%endif
/etc/archiving/rmarchived.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmarchived
/etc/rsyslog.d/11-rmarchived.conf
/etc/archiving/conf.d/retention_database.conf
/etc/archiving/conf.d/retention_delete_database.conf
/etc/archiving/conf.d/cdrmunch_database.conf
/etc/archiving/conf.d/internal_database.conf
/var/www/rmarchived
/etc/httpd/conf.d/rmarchived.conf

%files platform-config-redmatter
%defattr(-,root,root)
/etc/archiving/conf.d/retention-purge-redmatter.conf

%files platform-config-ipc
%defattr(-,root,root)
/etc/archiving/conf.d/retention-purge-ipc.conf

%files collectd
%defattr(-,root,root)
/usr/bin/rmcollectd.bin
/usr/bin/rmcollectd_safe
/usr/share/archiving/rmcollectd.supp
%if 0%{?el5}
/etc/rc.d/init.d/rmcollectd
%endif
%if 0%{?el7}
%{_unitdir}/rmcollectd.service
%endif
/etc/archiving/rmcollectd.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmcollectd
/etc/rsyslog.d/11-rmcollectd.conf
/etc/archiving/conf.d/sqlite.conf
/usr/share/archiving/blank_collectd.db3
/usr/share/archiving/bin/sqlite3

%files collectd-schema
%defattr(-,root,root)
/usr/share/archiving/collectd-schema

%files collect-smsd
%defattr(-,root,root)
/usr/bin/rmcollect-smsd.bin
/usr/bin/rmcollect-smsd_safe
/usr/share/archiving/rmcollect-smsd.supp
%if 0%{?el5}
/etc/rc.d/init.d/rmcollect-smsd
%endif
%if 0%{?el7}
%{_unitdir}/rmcollect-smsd.service
%endif
/etc/archiving/rmcollect-smsd.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmcollect-smsd
/etc/rsyslog.d/11-rmcollect-smsd.conf

%files collect-importd
%defattr(-,root,root)
/usr/bin/rmcollect-importd.bin
/usr/bin/rmcollect-importd_safe
/usr/share/archiving/rmcollect-importd.supp
%if 0%{?el5}
/etc/rc.d/init.d/rmcollect-importd
%endif
%if 0%{?el7}
%{_unitdir}/rmcollect-importd.service
%endif
/etc/archiving/rmcollect-importd.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
%dir %attr(777,cdrmunch,cdrmunch) /mnt/rmfs/archiving-import
/etc/logrotate.d/rmcollect-importd
/etc/rsyslog.d/11-rmcollect-importd.conf
%dir %attr(777,cdrmunch,cdrmunch) /mnt/rmfs/phonero
%dir %attr(777,cdrmunch,cdrmunch) /mnt/rmfs/phonero/download
%dir %attr(777,cdrmunch,cdrmunch) /mnt/rmfs/phonero/queue
%dir %attr(777,cdrmunch,cdrmunch) /mnt/rmfs/phonero/not-handled
/etc/archiving/phonero-cron.conf
/usr/bin/phonero-cron.phar
/etc/rsyslog.d/11-archiving-phonero-cron.conf
/etc/cron.1min/phonero-cron.phar

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

%files cli
%defattr(-,root,root)
/usr/bin/archiving-cli
/usr/bin/archiving-cli-*

%files common
%attr(755,root,root) /usr/bin/rmarchived-hAPI
%dir /etc/archiving/conf.d/

%files estuary
%defattr(-,root,root)
/usr/bin/rmestuary.bin
%if 0%{?el5}
/etc/rc.d/init.d/rmestuary
%endif
%if 0%{?el7}
%{_unitdir}/rmestuary.service
%endif
/etc/archiving/rmestuary.conf
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmestuary
/etc/rsyslog.d/11-rmestuary.conf

%files scripts
%defattr(-,root,root)
/usr/share/php/archiving/*
/usr/bin/archiving
/usr/bin/archiving-checker
/usr/bin/archiving-checker-feed.sh
/usr/bin/archiving-extract
/usr/bin/archiving-sms-recovery
/usr/bin/archiving-headerspatch
/usr/bin/archiving-purge-policy-fixer
/usr/bin/archiving-export
/usr/share/archiving/archiving-checker.conf.sample
/usr/share/archiving/archiving-extract.conf.sample
/usr/share/archiving/archiving-sms-recovery.conf.sample
/usr/share/archiving/archiving-headerspatch.conf.sample
/etc/archiving/scripts-retention_ro.conf
/etc/archiving/scripts-retention_rw.conf
/etc/archiving/purge-policy-fixer.conf
/etc/archiving/export.conf
/etc/rsyslog.d/11-archiving-checker.conf
/etc/rsyslog.d/11-archiving-extract.conf
/etc/rsyslog.d/11-archiving-sms-recovery.conf
/etc/rsyslog.d/11-archiving-headerspatch.conf
/etc/rsyslog.d/11-archiving-purge-policy-fixer.conf
/etc/rsyslog.d/11-archiving-export.conf
/etc/logrotate.d/archiving-headerspatch
/etc/logrotate.d/archiving-purge-policy-fixer
/etc/logrotate.d/archiving-export
%if 0%{?el5}
/etc/rc.d/init.d/rmarchpolfix
/etc/rc.d/init.d/rmarchexport
/etc/rc.d/init.d/rmarchpcapimport
%endif
%if 0%{?el7}
%{_unitdir}/rmarchpolfix.service
%{_unitdir}/rmarchexport.service
%{_unitdir}/rmarchpcapimport.service
%endif
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving/pcap-import
%dir %attr(755,cdrmunch,cdrmunch) /var/lib/archiving/pcap-import/done
/usr/bin/archiving-pcap-recording-import
/etc/archiving/conf.d/big_database.conf
/etc/archiving/pcap-recording-import.conf
/etc/rsyslog.d/11-archiving-pcap-recording-import.conf
/etc/logrotate.d/archiving-pcap-recording-import
%attr(755,root,root) /usr/bin/archiving-dsr-pre-processor
/etc/archiving/dsr-preproc.conf
/usr/share/php/archiving/archdsrpreproc_autoload.php
%attr(755,root,root) /etc/rc.d/init.d/rmarchdsrpreproc
/etc/rsyslog.d/11-archiving-dsr-pre-processor.conf
/etc/logrotate.d/archiving-dsr-pre-processor

%files qascripts
%defattr(-,root,root)
%attr(755,root,root) /usr/bin/archiving-makepurgedata
%attr(755,root,root) /usr/bin/send-test-eml.php
/usr/share/archiving/archiving-makepurgedata.conf.sample
/usr/share/archiving/collectd-cdrgen/*
/etc/rsyslog.d/11-archiving-makepurgedata.conf
/etc/logrotate.d/archiving-makepurgedata

%files purge-verify
%defattr(-,root,root)
/usr/bin/archiving-verifypurge
/usr/share/archiving/archiving-verifypurge.conf.sample
/etc/rsyslog.d/11-archiving-verifypurge.conf
/etc/logrotate.d/archiving-verifypurge
%attr(555,root,root) /usr/bin/archiving-make-purge-verification-csv
/etc/archiving/make-purge-verification-csv.conf
/usr/share/archiving/purge-verify-definition-schema.json
/usr/share/archiving/purge-verify-definition.example.json

%files eml2a
%defattr(-,root,root)
%dir /etc/archiving
%dir %attr(755,postfix,postfix) /var/run/eml2a
%dir %attr(755,postfix,postfix) /var/lib/eml2a
%dir %attr(755,postfix,postfix) /var/lib/eml2a/messages
%dir %attr(755,postfix,postfix) /usr/share/eml2a
/etc/archiving/eml2a.conf
/usr/share/eml2a/eml2a-milter.conf
%attr(4755,postfix,postfix) /usr/bin/eml2a
%attr(4755,postfix,postfix) /usr/bin/eml2a-milter
/usr/bin/eml2a-milter_safe
%if 0%{?el5}
%attr(755,root,root) /etc/rc.d/init.d/eml2a-milter
%endif
%if 0%{?el7}
%{_unitdir}/eml2a-milter.service
%endif
/etc/logrotate.d/eml2a
/etc/rsyslog.d/11-eml2a.conf
%attr(755,root,root) /usr/bin/eml2a-milter-hAPI

# ----------

%clean
rm -fR %{buildroot}

