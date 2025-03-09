#!/bin/bash

set -e

TARGET=x86_64-w64-mingw32

git clone https://github.com/FrodeSolheim/capsimg.git --depth=1 -b v5.1.2 \
    -c advice.detachedHead=false
cd capsimg
./bootstrap
./configure --host=${TARGET}
sed -i 's/\-g /-pipe -fPIC /' CAPSImg/Makefile

make -C CAPSImg -j$(nproc)
find . -name '*.dll' | cut -f 1 -d : | xargs ${TARGET}-strip -s
find . -name '*.a' | xargs ${TARGET}-strip -g

sed -i -e '/Linux change/,+5d' Core/CommonTypes.h
