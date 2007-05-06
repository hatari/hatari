#
# RPM spec file for Hatari
#
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#

BuildRequires: bash bzip2 coreutils cpio cpp diffutils file filesystem findutils glibc glibc-devel grep groff gzip libgcc m4 make man mktemp patch readline sed tar unzip util-linux zlib zlib-devel SDL SDL-devel autoconf binutils gcc libtool rpm

Name:         hatari
URL:          http://hatari.sourceforge.net/
License:      GPL
Group:        System/Emulators/Other
Autoreqprov:  on
Version:      0.95
Release:      1
Summary:      an Atari ST emulator suitable for playing games
Source:       %{name}-%{version}.tar.bz2
#Patch:        %{name}-0.95.dif
BuildRoot:    %{_tmppath}/%{name}-%{version}-build

%description
Hatari is an emulator for the Atari ST, STE, TT and Falcon computers.
The Atari ST was a 16/32 bit computer system which was first released by Atari
in 1985. Using the Motorola 68000 CPU, it was a very popular computer having
quite a lot of CPU power at that time.
Unlike many other Atari ST emulators which try to give you a good environment
for running GEM applications, Hatari tries to emulate the hardware of a ST as
close as possible so that it is able to run most of the old ST games and demos.

%prep
%setup
#%patch

%build
# LDFLAGS="-static" LIBS=`sdl-config --static-libs` \
CFLAGS="-O3 -fomit-frame-pointer" \
 ./configure --prefix=/usr --sysconfdir=/etc
make

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
install -d -m 755 ${RPM_BUILD_ROOT}%_mandir/man1
install -m 644 doc/hatari.1 ${RPM_BUILD_ROOT}%_mandir/man1
install -d -m 755 ${RPM_BUILD_ROOT}%_docdir/%{name}
install -m 644 *.txt doc/*.txt doc/*.html ${RPM_BUILD_ROOT}%_docdir/%{name}/
install -d -m 755 ${RPM_BUILD_ROOT}%_docdir/%{name}/images
install -m 644 doc/images/*.png ${RPM_BUILD_ROOT}%_docdir/%{name}/images/

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
/usr/bin/hatari
/usr/share/hatari
%doc %_mandir/man1/hatari.1*
#%doc *.txt doc/*.txt doc/*.html
%dir %_docdir/%{name}
%_docdir/%{name}/*.txt
%_docdir/%{name}/*.html
%dir %_docdir/%{name}/images
%_docdir/%{name}/images/*.png

%changelog -n hatari

* Sun May 06 2007 - thothy@users.sourceforge.net
- Adapted spec file to be able to build Hatari with RedHat, too

* Sun Aug 27 2006 - thothy@users.sourceforge.net
- Upgraded to version 0.90

* Tue Oct 18 2005 - thothy@users.sourceforge.net
- initial package
