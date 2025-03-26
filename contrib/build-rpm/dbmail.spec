%define         services dbmail-imapd dbmail-pop3d dbmail-lmtpd dbmail-sieved

%define		SRCBASE	%{_builddir}/%{name}-%{version}
%define		TMPLIB	%{buildroot}/usr/lib/tmpfiles.d
%define		SOURCE1	%{SRCBASE}/systemd/dbmail-imapd.service
%define		SOURCE2 %{SRCBASE}/systemd/dbmail-pop3d.service
%define		SOURCE3	%{SRCBASE}/systemd/dbmail-lmtpd.service
%define		SOURCE4	%{SRCBASE}/systemd/dbmail-sieved.service
%define		SOURCE5	%{SRCBASE}/dbmail.cron
%define		SOURCE6	%{SRCBASE}/dbmail.logrotate
%define		SOURCE7	%{SRCBASE}/dbmail.sysconfig
%define		SOURCE8	%{SRCBASE}/README.fedora
%define		SOURCE9	%{SRCBASE}/dbmail.conf

Name:           dbmail
Version:        3.5.0
Release:        2%{?dist}
Summary:        A database backed mail storage system

Group:          System Environment/Daemons
# db_getopot.c is licensed MIT
License:        GPLv2+ and MIT
URL:            http://www.dbmail.org
Source0:        dbmail-3.5.0.tar.gz

#Patch0:         dbmail-3.0.2-gthread.patch

BuildRoot:      %{buildrootdir}/%{name}-%{version}-%{release}.%{_arch}

BuildRequires:  gmime-devel > 2.4
BuildRequires:  openssl-devel >= 0.9.7a
BuildRequires:  glib2-devel >= 2.16, mhash-devel
BuildRequires:  libsieve-devel >= 2.1.13, libzdb-devel >= 2.4, libevent-devel
BuildRequires:  openldap-devel
BuildRequires:  asciidoc, xmlto

Requires:       glib2 >= 2.8, logrotate, cronie
Requires:       /usr/sbin/sendmail
# libzdb 2.5 is broken for postgresql
Requires:       libzdb >= 2.6
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units
# For triggerun
Requires(post): systemd-sysv

# Always require sqlite so a "out of the box" experience is possible.
#Requires:       sqlite

# Keep the obsoletes for upgrades
Obsoletes:      dbmail-sqlite < 2.2.5
Obsoletes:      dbmail-pgsql < 2.3.6
Obsoletes:      dbmail-mysql < 2.3.6
# Try to minimize dependency issues
Provides:       dbmail-sqlite = %{version}
Provides:       dbmail-pgsql = %{version}
Provides:       dbmail-mysql = %{version}

Requires(pre):  shadow-utils

%description
Dbmail is the name of a group of programs that enable the possiblilty of
storing and retrieving mail messages from a database.

Currently dbmail supports the following database backends via libzdb:
MySQL
PostgreSQL
SQLite

Please see /usr/share/doc/dbmail-*/README.fedora for specific information on
installation and configuration in Fedora.

%prep
%setup -q

%if 0%{?rhel} && 0%{?rhel} > 5
# Temporary patch - gmime is not adding flags for gthread; add to
# glib for now
%patch0 -p1 -b .gthread
%endif

# we don't need README.solaris and we don't want it caught up in the %%doc
# README* wildcard - but we do want our shiny new README.fedora file to be
# installed
rm -f README.solaris
install -p -m 644 %SOURCE8 %{buildroot}

# make a couple of changes to the default dbmail.conf file:
# 1. default driver/authdriver sqlite/sql
# 2. effective uid/gid to dbmail/dbmail
sed -i 's/\(^driver\W*=\)\(\W*$\)/\1 sqlite/' dbmail.conf
sed -i -e 's,\(^db\W*=\)\(.*$\),\1 %{_localstatedir}/lib/dbmail/dbmail.db,'   \
       -e 's/\(^authdriver\W*=\)\(\W*$\)/\1 sql/'                             \
       -e 's/\(^EFFECTIVE_USER\W*=\)\(.*$\)/\1 dbmail/'                       \
       -e 's/\(^EFFECTIVE_GROUP\W*=\)\(.*$\)/\1 dbmail/' dbmail.conf

%if 0%{?fedora} && 0%{?fedora} > 13
sed -i 's/gmime-2.4/gmime-2.6/g' configure
%endif

%build
export CFLAGS="%{optflags} -I/usr/include/zdb"
%configure --disable-rpath \
           --disable-static \
           --with-ldap \
           --with-sieve \
           --enable-manpages \
	   --enable-systemd

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{_mandir}/man{1,5,8}
mkdir -p $RPM_BUILD_ROOT/%{_unitdir}
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/cron.daily
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/logrotate.d
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig
mkdir -p $RPM_BUILD_ROOT/%{_localstatedir}/lib/dbmail
install -p -m 644 %SOURCE1 $RPM_BUILD_ROOT/%{_unitdir}
install -p -m 644 %SOURCE2 $RPM_BUILD_ROOT/%{_unitdir}
install -p -m 644 %SOURCE3 $RPM_BUILD_ROOT/%{_unitdir}
install -p -m 644 %SOURCE4 $RPM_BUILD_ROOT/%{_unitdir}
install -p -m 755 %SOURCE5 $RPM_BUILD_ROOT/%{_sysconfdir}/cron.daily/dbmail
install -p -m 644 %SOURCE6 $RPM_BUILD_ROOT/%{_sysconfdir}/logrotate.d/dbmail
install -p -m 644 %SOURCE7 $RPM_BUILD_ROOT/%{_sysconfdir}/sysconfig/dbmail 
install -p -m 600 %SOURCE9 $RPM_BUILD_ROOT/%{_sysconfdir}/
install -p -m 644 man/*1 $RPM_BUILD_ROOT/%{_mandir}/man1/
install -p -m 644 man/*5 $RPM_BUILD_ROOT/%{_mandir}/man5/
install -p -m 644 man/*8 $RPM_BUILD_ROOT/%{_mandir}/man8/
# remove libtool archives and -devel type stuff (but leave loadable modules)
find $RPM_BUILD_ROOT -name \*\.la -print | xargs rm -f
rm -f $RPM_BUILD_ROOT/%{_libdir}/dbmail/libdbmail.so
rm -rf %{TMPLIB}

%clean
rm -rf $RPM_BUILD_ROOT

%pre
getent group %{name} >/dev/null || groupadd -r %{name}
getent passwd %{name} >/dev/null || \
  useradd -r -M -g %{name} -d / -s /sbin/nologin \
  -c "DBMail Daemon" %{name}
exit 0

%post
if [ $1 -eq 1 ] ; then 
    # Initial installation 
    /bin/systemctl daemon-reload >/dev/null 2>&1 || :
fi
/sbin/ldconfig

%preun
if [ $1 -eq 0 ]; then
  for s in %services; do
    # Package removal, not upgrade
    /bin/systemctl --no-reload disable $s.service > /dev/null 2>&1 || :
    /bin/systemctl stop $s.service > /dev/null 2>&1 || :
  done
fi

%postun
/sbin/ldconfig
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ] ; then
  for s in %services; do
    # Package upgrade, not uninstall
    /bin/systemctl try-restart $s.service >/dev/null 2>&1 || :
  done
fi

%triggerun -- dmail < 3.2.3-3
# Save the current service runlevel info
# User must manually run systemd-sysv-convert --apply dbmail-imapd
# and system-sysv-convert --apply dmail-lmtpd
# and system-sysv-convert --apply dmail-pop3d
# and system-sysv-convert --apply dmail-sieved
# to migrate them to systemd targets
for s in %services; do
  /usr/bin/systemd-sysv-convert --save $s >/dev/null 2>&1 ||:
done

# Run these because the SysV package being removed won't do them
for s in %services; do
  /sbin/chkconfig --del $s >/dev/null 2>&1 || :
  /bin/systemctl try-restart $s.service >/dev/null 2>&1 || :
done


%files
%defattr(-,root,root,-)
%doc AUTHORS BUGS ChangeLog COPYING INSTALL README* THANKS UPGRADING sql dbmail.schema
%{_sbindir}/*
%{_mandir}/man1/*
%{_mandir}/man5/*
%{_mandir}/man8/*
%dir %{_libdir}/dbmail
%{_libdir}/dbmail/libauth_sql*
%{_libdir}/dbmail/libdbmail*
%{_libdir}/dbmail/libsort_sieve*
%config(noreplace) %{_sysconfdir}/dbmail.conf
%{_unitdir}/dbmail-*.service
%{_sysconfdir}/cron.daily/dbmail
%config(noreplace) %{_sysconfdir}/sysconfig/dbmail
%config(noreplace) %{_sysconfdir}/logrotate.d/dbmail
%dir %attr(0775,root,dbmail) /var/lib/dbmail

%package auth-ldap
Summary:        A database backed mail storage system - ldap authentication plugin
Group:          System Environment/Daemons
Requires:       dbmail = %{version}-%{release}, openldap

%description auth-ldap
This is the auth-ldap libraries for authentication against a ldap server with
dbmail.

%files auth-ldap
%defattr(-,root,root,-)
%attr(-,root,root) %{_libdir}/dbmail/libauth_ldap*


%changelog
* Wed Jul 18 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.0.2-4
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Fri Apr 20 2012 Jon Ciesla <limburgher@gmail.com> - 3.0.2-3
- Migrate to systemd, BZ 722341.

* Mon Mar 19 2012 Bernard Johnson <bjohnson@symetrix.com> - 3.0.2-2
- EL patch reworked for gthread only

* Sun Mar 18 2012 Bernard Johnson <bjohnson@symetrix.com> - 3.0.2-1
- 3.0.2

* Fri Jan 13 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.0.0-0.7.rc3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_17_Mass_Rebuild

* Mon Aug 15 2011 Bernard Johnson <bjohnson@symetrix.com> - 3.0.0-0.6.rc3
- 3.0.0-rc3

* Mon Apr 25 2011 Tom Callaway <spot@fedoraproject.org> - 3.0.0-0.5.rc2
- update to rc2
- apply compile fix from upstream git (patch3)

* Mon Feb 21 2011 Bernard Johnson <bjohnson@symetrix.com> - 3.0.0-0.4.rc1
- try rebuild against libevent ABI v2

* Tue Feb 08 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.0.0-0.3.rc1
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild

* Sat Jan 22 2011 Bernard Johnson <bjohnson@symetrix.com> - 3.0.0.0.2.rc1
- patch to glib package config flags for el6

* Sat Jan 15 2011 Bernard Johnson <bjohnson@symetrix.com> - 3.0.0-0.1.rc1
- v 3.0.0 release candidate 1

* Sun Sep 12 2010 Bernard Johnson <bjohnson@symetrix.com> - 2.2.17-1
- v  2.2.17
- drop unneeded patches

* Sun Aug 08 2010 Bernard Johnson <bjohnson@symetrix.com> - 2.2.16-1
- v 2.2.16
- drop unneeded patches
- spurious-imap-whitepatch patch
- backport IMAP-ID patch
- update asciidocs patch
- imap append-speedup patch
- inverse pop3 list patch

* Fri Apr 16 2010 Bernard Johnson <bjohnson@symetrix.com> - 2.2.15-2
- clip to zero patch
- query regression patch
- stack smash patch
- long running iquery patch

* Sun Feb 14 2010 Bernard Johnson <bjohnson@symetrix.com> - 2.2.15-1
- v 2.2.15
- remove patches upstreamed
- patches for EL5 to remove new md5 implementation

* Wed Oct 07 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-10
- add patch to keep from mangling mime parts (bz #527690)

* Fri Aug 21 2009 Tomas Mraz <tmraz@redhat.com> - 2.2.11-9
- rebuilt with new openssl

* Fri Jul 24 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.2.11-8
- Rebuilt for https://fedoraproject.org/wiki/Fedora_12_Mass_Rebuild

* Mon Jul 06 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-7
- fix left out ? in comparison

* Sun Jul 05 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-6
- fix conditional comparison to be 0 for no value

* Sun Jul 05 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-5
- patch to remove duplicate email boxes from being listed
- consider cron file a config file
- add -b option to cron job to rebuild body/header/envelope cache tables
- change order of redirection in cron job
- fix typo in dbmail-pop3d that causes LSB info to not be recognized
- add provides for dbmail-sqlite
- conditional to compile with gmime22 when needed

* Thu Mar 19 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-4
- build agaist old gmime22 (bz #490316)

* Tue Feb 24 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.2.11-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild

* Thu Feb 05 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-2
- change BR from openssl to openssl-devel

* Tue Feb 03 2009 Bernard Johnson <bjohnson@symetrix.com> - 2.2.11-1
- v 2.2.11
- updated summaries
- fix bug in dbmail-pop3d init script

* Mon Jul  7 2008 Tom "spot" Callaway <tcallawa@redhat.com> - 2.2.9-2
- fix conditional comparison

* Thu Apr 24 2008 Bernard Johnson <bjohnson@symetrix.com> - 2.2.9-1
- v 2.2.9

* Mon Feb 18 2008 Fedora Release Engineering <rel-eng@fedoraproject.org> - 2.2.8-2
- Autorebuild for GCC 4.3

* Fri Jan 18 2008 Bernard Johnson <bjohnson@symetrix.com> - 2.2.8-1
- 2.2.8-1

* Thu Dec 06 2007 Release Engineering <rel-eng at fedoraproject dot org> - 2.2.7-2
 - Rebuild for deps

* Wed Oct 31 2007 Bernard Johnson <bjohnson@symetrix.com> - 2.2.7-1
- 2.2.7-1
- removed unused thread references patch
- removed unused hup patch
- removed unused gmime segv patch
- license clarification
- dbmail: Initscript Review (bz #246901)

* Tue Aug 28 2007 Fedora Release Engineering <rel-eng at fedoraproject dot org> - 2.2.5-7
- Rebuild for selinux ppc32 issue.

* Tue Jul 03 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-6
- patch to fix SEGV in dbmail-imapd

* Sat Jun 23 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-5
- patch to reopen logs files on -HUP
- patch to send error when thread references requested
- don't filter libdbmail.so*

* Sat Jun 23 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-4
- kill ld.so config
- filter private libraries from provides (bz#245326)

* Wed Jun 20 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-3
- assign uid from package user registry (bz#244611)

* Tue Jun 05 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-2
- fix %%setup directory

* Tue Jun 05 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-1
- 2.2.5
- change method of restarting daemons to that suggested in dbmail bug #600

* Wed May 23 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.5-0.1.rc3
- update to svn 2.2.5rc3
- remove unneccessary patches
- make sqlite default driver for better out of the box experience

* Thu Mar 23 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.4-4
- actually APPLY the short write patch

* Thu Mar 22 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.4-3
- patch to eliminate short write messages
- use /sbin/service instead of running init scripts directly
- requires for initscripts because daemon function in initfile requires it
- modern tarballs do not require xmlto and asciidoc to build the docs
- change conditionals to give everything sqlite support unless it's built in
  the fedora buildsystem and %%{fedora} < 4

* Tue Mar 20 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.4-2
- patch to fix expunge bug

* Tue Mar 13 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.4-1
- v. 2.2.4
- remove umask patch as it's included upstream now

* Wed Feb 28 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.3-1
- v. 2.2.3
- tab removal in dbmail.conf no longer required
- libsqlite.so in not built anymore unless specified, remove fix
- libauth-ldap.so wasn't be built properly, fixed
- rework umask patch, still want a stronger umask on log files

* Tue Feb 20 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-9
- fix source0 location

* Tue Feb 20 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-8
- change /etc/dbmail.conf to mode 0600
- remove README.solaris, create README.fedora
- add ref to README.fedora in %%desc

* Tue Feb 20 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-7
- make macro tests a little more readable
- change dbmail-database to dbmail-database-driver; more descriptive
- reduce gmime reqs to 2.1.19
- specify sqlite req at 3 or greater

* Sun Feb 18 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-6
- remove bogus require for main package on mysql
- virtual depend with exact %%{version}-%%{release}
- remove extra mysql-devel BR
- update description to include sqlite if built with sqlite
- for mysql, 4.1.3 is required, not just 4.1
- add requires for vixie-cron
- move database specific docs to database subpackages

* Sun Feb 18 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-5
- fix perms on man pages

* Sat Feb 17 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-4
- fix a few things in scriptlets for consistency
- send error output from logrotate HUP to /dev/null
- explicitly require initscripts since they all use the daemon function
- use explicit %%{version}-%%{release} for provides

* Mon Feb 05 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-3
- fix typo in logrotate script
- patch umask for log files to be something more reasonable

* Sat Jan 31 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-2
- add some conditionals for not building sqlite on some product releases
- substitude \t for tab in sed so that rpmlint doesn't complain about mixing
  tabs and spaces

* Sat Jan 31 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.2-1
- add logrotate for dbmail.err
- sub packages depend on %%{version}-%%{release}
- update to 2.2.2
- remove mailbox2dbmail patch
- translate tabs to space in dbmail.conf
- remove errno race patch

* Sat Jan 13 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-7
- revert to older fedora-usermgmt macros so rpm will work with older os

* Fri Jan 12 2007 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-6
- add patch to fix errno race condition
- don't delete libsort_sieve.so, it's a module

* Thu Dec 14 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-5
- fix my local svn that caused x bit on init files to sneak in

* Thu Dec 14 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-4
- cleanup of spec file
- use fedora-usermgmt hooks
- split and build all database libraries
- kill modules/.libs from the module load path

* Tue Dec 05 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-3
- leave the right .so files for modules

* Mon Nov 27 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-2
- update with Fedora Extras style spec file

* Sat Nov 18 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.1-1.sc
- version 2.2.1

* Mon Nov 15 2006 Bernard Johnson <bjohnson@symetrix.com> 2.2.0-4.sc
- release 2.2.0-1.sc
