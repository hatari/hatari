#!/bin/sh

set -e
set -x

SDLVERSION=3.4.4
PNGVERSION=1.6.48

if [ ! -d dl_cache ]; then
  mkdir dl_cache
fi

# Download and install precompiled SDL3 Framework
if [ ! -f dl_cache/SDL3-$SDLVERSION.dmg ]; then
  wget -O dl_cache/SDL3-$SDLVERSION.dmg \
    https://github.com/libsdl-org/SDL/releases/download/release-$SDLVERSION/SDL3-$SDLVERSION.dmg
  echo "SDL" > libs-changed.txt
fi
hdiutil attach dl_cache/SDL3-$SDLVERSION.dmg
sudo cp -a /Volumes/SDL3/SDL3.xcframework /Volumes/SDL3/share /Library/Frameworks/

# Download, compile and install libpng Framework
if [ ! -e dl_cache/png.framework ]; then
  wget -O dl_cache/libpng-$PNGVERSION.tar.xz \
    "http://prdownloads.sourceforge.net/libpng/libpng-$PNGVERSION.tar.xz?download" ;
  tar -xJf dl_cache/libpng-$PNGVERSION.tar.xz
  cd libpng-$PNGVERSION
  cmake -DPNG_FRAMEWORK=ON -DPNG_HARDWARE_OPTIMIZATIONS=OFF \
        -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING="10.13" \
        -DCMAKE_OSX_ARCHITECTURES:STRING="arm64;x86_64" .
  cmake --build . --verbose --config Release -j$(sysctl -n hw.ncpu)
  codesign --force -s - png.framework
  cd ..
  mv libpng-$PNGVERSION/png.framework dl_cache/
  rm -rf libpng-$PNGVERSION
  echo "png" >> libs-changed.txt
fi
sudo cp -a dl_cache/png.framework /Library/Frameworks/

# Download and install precompiled portmidi Framework
if [ ! -e dl_cache/portmidi.framework ]; then
  cd dl_cache
  wget "https://hatari.frama.io/ci-files/macos/portmidi.framework.zip"
  unzip portmidi.framework.zip
  mv license.txt portmidi-license.txt
  cd ..
  echo "portmidi" >> libs-changed.txt
fi
sudo cp -a dl_cache/portmidi.framework /Library/Frameworks/

# Download and install precompiled capsimage Framework
if [ ! -e dl_cache/capsimage_5.1_macos-x86_64-arm64 ]; then
  cd dl_cache
  wget "https://hatari.frama.io/ci-files/macos/capsimage_5.1_macos-x86_64-arm64_selfsigned.zip"
  unzip capsimage_5.1_macos-x86_64-arm64_selfsigned.zip
  cd capsimage_5.1_macos-x86_64-arm64
  mv LICENCE.txt DONATIONS.txt README.txt HISTORY.txt CAPSImage.framework
  cd ../..
  echo "capsimage" >> libs-changed.txt
fi
sudo cp -a dl_cache/capsimage_5.1_macos-x86_64-arm64/CAPSImage.framework /Library/Frameworks/
