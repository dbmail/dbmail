#
# spec file for package libsieve (Version 2.2)
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

Name:           libsieve
Version:        2.2
Release:        2%{?dist}
Summary:        A Library for Parsing, Sorting and Filtering Your Mail

Group:          Development/Libraries/Other
# License says LGPL, but source is a mix of LGPL and GPL, so we must use the
# more restrictive GPL tag for the license
License:        GPL
URL:            https://github.com/sodabrew/libsieve
Source0:        https://github.com/sodabrew/libsieve/archive/libsieve-2.2.zip
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
BuildRequires:  flex bison pkgconfig libtool


%description 
libSieve provides a library to interpret Sieve scripts, and to execute those
scripts over a given set of messages. The return codes from the libSieve
functions let your program know how to handle the message, and then it's up to
you to make it so. libSieve makes no attempt to have knowledge of how SMTP,
IMAP, or anything else work; just how to parse and deal with a buffer full of
emails. The rest is up to you!

Provides:       libsieve
Obsoletes:      libsieve < %{version}

%description
libSieve provides a library to interpret Sieve scripts, and to execute those
scripts over a given set of messages. The return codes from the libSieve
functions let your program know how to handle the message, and then it's up to
you to make it so. libSieve makes no attempt to have knowledge of how SMTP,
IMAP, or anything else work; just how to parse and deal with a buffer full of
emails. The rest is up to you!



%prep 
%setup -q -n %{name}-%{name}-%{version}/src

%build 
./bootstrap
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
%doc ../AUTHORS ../COPYING ../NEWS ../README

%package devel
Summary: A Library for Parsing, Sorting and Filtering Your Mail

Group: Development/Libraries/Other

Requires: libsieve = %{version}-%{release}

%description devel
libSieve provides a library to interpret Sieve scripts, and to execute those
scripts over a given set of messages. The return codes from the libSieve
functions let your program know how to handle the message, and then it's up to
you to make it so. libSieve makes no attempt to have knowledge of how SMTP,
IMAP, or anything else work; just how to parse and deal with a buffer full of
emails. The rest is up to you!

These are the development libraries.

%files devel
%defattr(-,root,root,-) 
%{_libdir}/*.so
%{_libdir}/pkgconfig/libsieve.pc
%{_includedir}/*

%changelog 
* Thu Nov 20 2014 -- furrylinx@gmail.com
— Initial package
