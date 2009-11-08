
cmake_minimum_required(VERSION 2.6)

project(Hatari)

find_package(SDL REQUIRED)

find_package(ZLIB)

find_package(PNG)

add_definitions(-DCONFDIR=\"/etc\")

add_subdirectory(src)
