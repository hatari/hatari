#!/bin/sh
#
# Hatari - macos-build.sh
# Copyright (C) 2026 by manni07
# Created: 2026-05-13
#
# SPDX-License-Identifier: GPL-2.0-or-later

set -e
set -x

PATH=/usr/local/bin:$PATH

mkdir build
cd build

# Configure the build and compile it
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING="10.13" \
      -DCMAKE_OSX_ARCHITECTURES:STRING="arm64;x86_64" .. \
  || { cat config.log; exit 1; }
cmake --build . --verbose --config Release -j$(sysctl -n hw.ncpu) -t Hatari

cp -a ../dl_cache/portmidi.framework src/hatari.app/Contents/Frameworks/
cp -a ../dl_cache/capsimage_5.1_macos-x86_64-arm64/CAPSImage.framework src/hatari.app/Contents/Frameworks/
codesign --force -s - --entitlements ../src/gui-osx/hatari.app.xcent src/Hatari.app

cd ..

# Create a package for the users
mkdir hatari-snapshot
echo $(git rev-parse HEAD) > hatari-snapshot/version.txt
date >> hatari-snapshot/version.txt
cp -a build/src/Hatari.app hatari-snapshot/
cp -a doc gpl.txt readme.txt hatari-snapshot/
cp dl_cache/portmidi-license.txt hatari-snapshot/
zip -r hatari-snapshot.zip hatari-snapshot
