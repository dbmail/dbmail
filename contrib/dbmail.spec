%define initdir %{_sysconfdir}/init.d

Summary: The DBMail Imap server.
Name: dbmail
Version: 2.0.10
Release: 1
License: GNU GPL Version 2
Group: System Environment/Daemons
URL: http://www.dbmail.org/
Source: http://www.dbmail.org/download/2.0/dbmail-%{version}.tar.gz

Packager: Jeroen Simonetti <jeroen@simonetti.nl>

# Red Hat style init scripts
Source1: dbmail-imapd.init
Source2: dbmail-pop3d.init
Source3: dbmail-lmtpd.init

Patch0: dbmail-removeversion.patch

Requires: mysql >= 4.1 mysql-server >= 4.1
BuildRequires: mysql-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
DBMail is an Imap server that uses a database as a back-end. This
build uses mysql.

%prep
%setup
%patch0 -p0

%build
%configure --with-mysql
%{__make} %{?_smp_mflags} \
	XCFLAGS="-fPIC -fomit-frame-pointer -DPIC"

%install
%{__rm} -rf %{buildroot}
%{__install} -d -m0755 %{buildroot}%{_libdir} \
		%{buildroot}%{_bindir} \
		%{buildroot}%{_includedir}
%makeinstall

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d

install -m744 -o92 -g92 dbmail.conf              \
      $RPM_BUILD_ROOT%{_sysconfdir}/dbmail.conf
install                 %{SOURCE1}               \
      $RPM_BUILD_ROOT%{_sysconfdir}/init.d/dbmail-imapd
install                 %{SOURCE2}               \
      $RPM_BUILD_ROOT%{_sysconfdir}/init.d/dbmail-pop3d
install                 %{SOURCE3}               \
      $RPM_BUILD_ROOT%{_sysconfdir}/init.d/dbmail-lmtpd

%post
/sbin/chkconfig --add dbmail-imapd
/sbin/chkconfig --add dbmail-pop3d
/sbin/chkconfig --add dbmail-lmtpd
/usr/sbin/useradd -c "DBMail Imap Account" -d /tmp -r -s /bin/false -u 92 dbmail


%preun
if [ $1 = 0 ] ; then
    /sbin/chkconfig --del dbmail-imapd
    /sbin/chkconfig --del dbmail-pop3d
    /sbin/chkconfig --del dbmail-lmtpd
    /sbin/service dbmail-imapd stop >/dev/null 2>&1
    /sbin/service dbmail-pop3d stop >/dev/null 2>&1
    /sbin/service dbmail-lmtpd stop >/dev/null 2>&1
fi
exit 0


%postun
if [ "$1" -ge "1" ]; then
	%{initdir}/dbmail-imapd condrestart >/dev/null 2>&1
	%{initdir}/dbmail-pop3d condrestart >/dev/null 2>&1
	%{initdir}/dbmail-lmtpd condrestart >/dev/null 2>&1
fi
/usr/sbin/userdel dbmail



%clean
%{__rm} -rf %{buildroot}



%files
%defattr(-,root,root)
%doc README COPYING ChangeLog
%doc TODO VERSION NEWS INSTALL AUTHORS EXTRAS 
%doc sql
%{_sbindir}/*
%{_mandir}/man1/*
%{_mandir}/man8/*
%defattr(-,root,root)
%config(noreplace) %{_sysconfdir}/dbmail.conf
%{_sysconfdir}/init.d/dbmail-imapd
%{_sysconfdir}/init.d/dbmail-pop3d
%{_sysconfdir}/init.d/dbmail-lmtpd
%{_libdir}/*

%changelog
* Thu Jun 15 2006 Jeroen Simonetti <jeroen@simonetti.nl> 2.0.10
- Update to version 2.0.10

* Wed Sep 7 2005 Jeroen Simonetti <jeroen@simonetti.nl> 2.0.7
- Update to version 2.0.7
- Added better init scripts
- Added patch to remove version identification

* Sat Aug 20 2005 Jeroen Simonetti <jeroen@simonetti.nl> 2.0.6
- Create RPM package for dbmail for Fedora 4. Uses MySQL and the
  database initialisation must be done manually.
