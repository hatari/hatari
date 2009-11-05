
cmake_minimum_required(VERSION 2.6)

project(Hatari)

Find_Package(SDL REQUIRED)

add_definitions(-DCONFDIR=\"/etc\")

add_subdirectory(src)
