#
# RPM spec file for Hatari
#
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#

Name:         hatari
URL:          http://hatari.tuxfamily.org/
License:      GPLv2+
Group:        System/Emulators/Other
Autoreqprov:  on
Version:      2.4.1
Release:      1
Summary:      An Atari ST/STE/TT/Falcon emulator
Source:       %{name}-%{version}.tar.bz2
BuildRoot:    %{_tmppath}/%{name}-%{version}-build
Prefix:       /usr

BuildRequires: binutils cmake coreutils cpio cpp diffutils file filesystem
BuildRequires: findutils gcc grep gzip libgcc make man patch sed util-linux
BuildRequires: glibc-devel zlib-devel SDL2-devel libpng-devel readline-devel

# Required by zip2st and atari-hd-image
Requires: unzip
Requires: mtools
Requires: dosfstools

%description
Hatari is an emulator for the Atari ST, STE, TT and Falcon computers.

The Atari ST was a 16/32 bit computer system which was first released by Atari
in 1985. Using the Motorola 68000 CPU, it was a very popular computer having
quite a lot of CPU power at that time.

Unlike most other open source ST emulators which try to give you a good
environment for running GEM applications, Hatari tries to emulate the hardware
as close as possible so that it is able to run most of the old Atari games
and demos.  Because of this, it may be somewhat slower than less accurate
emulators.

%prep
%setup
#%patch

%build
./configure --enable-werror --prefix=/usr
make %{?_smp_mflags}

%check
make test

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{_bindir}/*
%{_datadir}/%{name}
%{_datadir}/applications/*.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.*
%{_datadir}/icons/hicolor/*/mimetypes/*
%{_datadir}/mime/packages/hatari.xml
%{_mandir}/man1/*
%doc %{_pkgdocdir}
%license gpl.txt

%changelog -n hatari

* Wed Aug 03 2022 - Nicolas Pomarede
- Hatari version 2.4.1

* Sat Jul 09 2022 - Nicolas Pomarede
- Hatari version 2.4.0

* Sun Dec 27 2020 - Nicolas Pomarede
- Hatari version 2.3.1

* Sat Nov 28 2020 - Nicolas Pomarede
- Hatari version 2.3.0

* Fri Feb 08 2019 - Nicolas Pomarede
- Hatari version 2.2.1

* Thu Jan 31 2019 - Nicolas Pomarede
- Hatari version 2.2.0

* Wed Feb 07 2018 - Nicolas Pomarede
- Hatari version 2.1.0

* Fri Nov 04 2016 - Nicolas Pomarede
- Hatari version 2.0.0

* Thu Sep 10 2015 - Nicolas Pomarede
- Hatari version 1.9.0

* Wed Jul 30 2014 - Nicolas Pomarede
- Hatari version 1.8.0

* Mon Jun 24 2013 - Nicolas Pomarede
- Hatari version 1.7.0

* Sun Jun 24 2012 - Nicolas Pomarede
- Hatari version 1.6.2

* Fri Jan 13 2012 - Nicolas Pomarede
- Hatari version 1.6.1

* Sun Jan 01 2012 - Nicolas Pomarede
- Hatari "Happy New Year 2012" version 1.6.0

* Tue Jul 19 2011 - Nicolas Pomarede
- Hatari version 1.5.0

* Sat Jun 12 2010 - Nicolas Pomarede
- Hatari version 1.4.0

* Sat Sep 05 2009 - Thomas Huth
- Hatari version 1.3.1

* Sun Aug 16 2009 - Thomas Huth
- Hatari version 1.3.0

* Sat Jan 24 2009 - Thomas Huth
- Hatari version 1.2.0

* Sat Nov 29 2008 - Thomas Huth
- Hatari version 1.1.0

* Wed Jan 02 2008 - Thomas Huth
- Adapted RPM to the latest source code level (aiming at version 1.0.0)

* Sun May 06 2007 - Thomas Huth
- Adapted spec file to be able to build Hatari with RedHat, too

* Sun Aug 27 2006 - Thomas Huth
- Upgraded to version 0.90

* Tue Oct 18 2005 - Thomas Huth
- initial package
