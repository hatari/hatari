
cmake_minimum_required(VERSION 2.6)

project(Hatari)

find_package(SDL REQUIRED)

find_package(ZLIB)
if(ZLIB_FOUND)
	set(HAVE_LIBZ 1)
	set(HAVE_ZLIB_H 1)
endif(ZLIB_FOUND)

find_package(PNG)
if(PNG_FOUND)
	set(HAVE_LIBPNG 1)
endif(PNG_FOUND)

find_package(X11)
if(X11_FOUND)
	set(HAVE_X11 1)
endif(X11_FOUND)

add_definitions(-DCONFDIR=\"/etc\")

add_subdirectory(src)

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config-cmake.h ${CMAKE_BINARY_DIR}/config.h)
