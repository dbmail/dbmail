#
# spec file for package libzdb (Version 3.4.0)
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

Name:           libzdb
Version:        3.4.0
Release:        2%{?dist}
Summary:        The Zild C Database Library implements a small, fast, and easy to  use database API

Group:          Development/Libraries/Database
# License says LGPL, but source is a mix of LGPL and GPL, so we must use the
# more restrictive GPL tag for the license
License:        GPL
URL:            https://github.com/mverbert/libzdb
Source0:        https://www.tildeslash.com/libzdb/dist/%{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
BuildRequires:  flex bison mariadb-devel openssl-devel postgresql-devel pkgconfig libtool autoconf-archive

%description
    The Zild C Database Library implements a small, fast, and easy to
    use database API with thread-safe connection pooling. The library
    can connect transparently to multiple database systems, has zero
    configuration and connections are specified via a standard URL
    scheme.


%prep 
%setup -q

%build 
%configure 
make %{?_smp_mflags}


%install 
make install DESTDIR=$RPM_BUILD_ROOT INSTALL="%{__install} -c -p"
rm -f $RPM_BUILD_ROOT/%{_libdir}/*\.{a,la}


%clean 
rm -rf $RPM_BUILD_ROOT

%post  -p /sbin/ldconfig
%postun -p /sbin/ldconfig 

%files
%defattr(-,root,root,-) 
%{_libdir}/*.so.*
%doc AUTHORS COPYING README

%package devel
Summary: The Zild C Database Library implements a small, fast, and easy to  use database API

Group: Development/Libraries/Database

Requires: libzdb = %{version}-%{release}

%description devel
    The Zild C Database Library implements a small, fast, and easy to
    use database API with thread-safe connection pooling. The library
    can connect transparently to multiple database systems, has zero
    configuration and connections are specified via a standard URL
    scheme.

    These are the development libraries.

%files devel
%defattr(-,root,root,-) 
%{_libdir}/*.so
%{_libdir}/pkgconfig/zdb.pc
%{_includedir}/*

%changelog 
* Sat Apr 10 2021 -- cosmin.cioranu@gmail.com
— Initial package
