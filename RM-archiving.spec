%define version 0.0.1
%define appname archiving
%define buildtype debug
%define __os_install_post %{nil}

Group: RedMatter/Archiving
Name: RM-%{appname}
Prefix: /
Release: 0
Source: RM-%{appname}-%{version}.tar.gz
URL: http://www.redmatter.com/
Version: %{version}
Buildroot: /tmp/RM-%{appname}-%{version}
Summary: Red Matter %{appname}
License: Closed
Requires: boost >= 1.45.0, mysql++ >= 3.1.0, cryptopp
BuildRequires: boost-devel >= 1.45.0, mysql++-devel >= 3.1.0, RM-librml-devel >= 0.0.1-13011, libs3-devel, cryptopp-devel, pugixml

%package collectd
Summary: Archiving CollectD
Group: RedMatter/Archiving
Requires: boost >= 1.45.0, mysql++ >= 3.1.0, libs3, cryptopp

#%package migrationd
#Summary: Archiving CollectD
#Group: RedMatter/Archiving
#Requires: boost >= 1.45.0, mysql++ >= 3.1.0, libs3, cryptopp, php-s3, RM-php-rmapp >= 0.0.1-11853, RM-%{appname}-convert = %{version}-%{release}

%package convert
Summary: Archiving Convert Utility
Group: RedMatter/Archiving
Requires: boost >= 1.45.0, cryptopp

%description
Archiving daemon ( ArchiveD )

%description collectd
Works on CDR from FreeSWITCH to identify files to be archived.
It also makes sense of archiving policy and passes that on to ArchiveD.

#%description migrationd
#Archiving MigrationD

%description convert
Archiving Convert Utility; used by core-api to fetch files from S3

%prep
%setup -q 

%build
echo %{version} | awk -F\. '{print "VERSION_MA="$1"\nVERSION_MI="$2"\nVERSION_MIN="$3"\n"}' > version_info
echo "BUILD_ID=%{release}" >> version_info 
make TMP=tmp-%{buildtype}-build BUILD_MODE=`echo %{buildtype} | tr 'a-z' 'A-Z'`

%install
mkdir -p %{buildroot}/usr/bin/
mkdir -p %{buildroot}/etc/archiving/conf.d/
mkdir -p %{buildroot}/etc/logrotate.d/
mkdir -p %{buildroot}/etc/rsyslog.d/
mkdir -p %{buildroot}/etc/rc.d/init.d/
mkdir -p %{buildroot}/usr/share/archiving/migration-scripts
mkdir -p %{buildroot}/var/lib/archiving
mkdir -p %{buildroot}/var/run/archiving

# ArchiveD
cp archived/tmp-%{buildtype}-build/archived %{buildroot}/usr/bin/rmarchived.bin
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

## MigrationD
#cp migrationd/tmp-%{buildtype}-build/migrationd %{buildroot}/usr/bin/rmmigrationd.bin
#cp migrationd/rmmigrationd_safe %{buildroot}/usr/bin/
#cp migrationd/rmmigrationd %{buildroot}/etc/rc.d/init.d/
#cp migrationd/rmmigrationd.supp %{buildroot}/usr/share/archiving/
#cp migrationd/scripts/* %{buildroot}/usr/share/archiving/migration-scripts/.
#cp migrationd/rmmigrationd.conf %{buildroot}/etc/archiving/
#cp migrationd/rmmigrationd_migration_database.conf %{buildroot}/etc/archiving/rmmigrationd_migration_database.conf.sample
#cp migrationd/rmmigrationd_retention_database.conf %{buildroot}/etc/archiving/rmmigrationd_retention_database.conf.sample
#cp migrationd/logrotate.conf %{buildroot}/etc/logrotate.d/rmmigrationd
#cp migrationd/rsyslog.conf %{buildroot}/etc/rsyslog.d/11-rmmigrationd.conf

# Convert
cp convert/tmp-%{buildtype}-build/convert %{buildroot}/usr/bin/archiving-convert

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

#%pre migrationd
#if grep ^cdrmunch: /etc/group &>/dev/null; then
#	: # group already exists
#else
#	/usr/sbin/groupadd -g 2051 cdrmunch || exit
#fi
#
#if ! /usr/bin/id cdrmunch &>/dev/null; then
#	/usr/sbin/useradd -r -g cdrmunch -s /bin/false -d /var/log/cdrmunch -u 2051 cdrmunch || exit
#fi

%post
/sbin/chkconfig --add rmarchived
/sbin/chkconfig rmarchived on
[ -x /etc/init.d/rsyslog ] && /etc/init.d/rsyslog restart || echo "**WARNING** rsyslog not found; consider installing it."

%post collectd
/sbin/chkconfig --add rmcollectd
/sbin/chkconfig rmcollectd on
[ -x /etc/init.d/rsyslog ] && /etc/init.d/rsyslog restart || echo "**WARNING** rsyslog not found; consider installing it."

#%post migrationd
#/sbin/chkconfig --add rmmigrationd
#/sbin/chkconfig rmmigrationd on
#[ -x /etc/init.d/rsyslog ] && /etc/init.d/rsyslog restart || echo "**WARNING** rsyslog not found; consider installing it."

%postun
[ -x /etc/init.d/rsyslog ] && /etc/init.d/rsyslog restart || /bin/true

%files
%defattr(-,root,root) 
/usr/bin/rmarchived.bin
/usr/bin/rmarchived_safe
/usr/share/archiving/rmarchived.supp
/etc/rc.d/init.d/rmarchived
%config(noreplace) /etc/archiving/rmarchived.conf
%dir /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmarchived
/etc/rsyslog.d/11-rmarchived.conf

%files collectd
%defattr(-,root,root) 
/usr/bin/rmcollectd.bin
/usr/bin/rmcollectd_safe
/usr/share/archiving/rmcollectd.supp
/etc/rc.d/init.d/rmcollectd
%config(noreplace) /etc/archiving/rmcollectd.conf
%dir /var/lib/archiving
%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
/etc/logrotate.d/rmcollectd
/etc/rsyslog.d/11-rmcollectd.conf

#%files migrationd
#%defattr(-,root,root) 
#/usr/bin/rmmigrationd.bin
#/usr/bin/rmmigrationd_safe
#/usr/share/archiving/rmmigrationd.supp
#/etc/rc.d/init.d/rmmigrationd
#%config(noreplace) /etc/archiving/rmmigrationd.conf
#/etc/archiving/rmmigrationd_migration_database.conf.sample
#/etc/archiving/rmmigrationd_retention_database.conf.sample
#%dir /var/lib/archiving
#%dir %attr(755,cdrmunch,cdrmunch) /var/run/archiving
#/etc/logrotate.d/rmmigrationd
#/etc/rsyslog.d/11-rmmigrationd.conf
#%dir /usr/share/archiving/migration-scripts
#/usr/share/archiving/migration-scripts/*

%files convert
%defattr(-,root,root) 
/usr/bin/archiving-convert

%clean
rm -fR %{buildroot}

# build me 2011.05.17.13.15

