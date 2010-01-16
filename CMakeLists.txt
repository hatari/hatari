
cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

project(Hatari)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Set build type to "Release" if user did not specify any build type yet
# Other possible values: Debug, Release, RelWithDebInfo and MinSizeRel
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

# ##########################
# Conditional build features
# ##########################

set(ENABLE_DSP_EMU 1
    CACHE BOOL "Enable DSP 56k emulator for Falcon mode")
set(ENABLE_TRACING 1
    CACHE BOOL "Enable tracing messages for debugging")
set(ENABLE_SMALL_MEM 0
    CACHE BOOL "Enable to use less memory - at the expense of emulation speed")

# ####################
# Check for libraries:
# ####################

find_package(SDL REQUIRED)
if(NOT SDL_FOUND)
	message(FATAL_ERROR "SDL library not found!")
endif(NOT SDL_FOUND)

find_package(Readline)
if(READLINE_FOUND)
	set(HAVE_LIBREADLINE 1)
endif(READLINE_FOUND)

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

find_package(PortAudio)
if(PORTAUDIO_FOUND)
	set(HAVE_PORTAUDIO 1)
endif(PORTAUDIO_FOUND)

# ###########################
# Check for optional headers:
# ###########################

check_include_files(termios.h HAVE_TERMIOS_H)
check_include_files(glob.h HAVE_GLOB_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(malloc.h HAVE_MALLOC_H)
check_include_files(SDL/SDL_config.h HAVE_SDL_SDL_CONFIG_H)
check_include_files(sys/times.h HAVE_SYS_TIMES_H)
check_include_files("sys/socket.h;sys/un.h" HAVE_UNIX_DOMAIN_SOCKETS)

# #############################
# Check for optional functions:
# #############################

check_function_exists(cfmakeraw HAVE_CFMAKERAW)
check_function_exists(setenv HAVE_SETENV)
check_function_exists(select HAVE_SELECT)

# ################
# CPP Definitions:
# ################

add_definitions(-DCONFDIR=\"/etc\")

# Test for large file support
execute_process(COMMAND getconf LFS_CFLAGS
                OUTPUT_VARIABLE DETECTED_LFS_CFLAGS)
if(DETECTED_LFS_CFLAGS)
	add_definitions(${DETECTED_LFS_CFLAGS})
endif(DETECTED_LFS_CFLAGS)

# #############
# Other CFLAGS:
# #############

# Warning flags:
if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "-Wcast-qual -Wbad-function-cast -Wpointer-arith ${CMAKE_C_FLAGS}")
	set(CMAKE_C_FLAGS "-Wmissing-prototypes -Wstrict-prototypes ${CMAKE_C_FLAGS}")
	set(CMAKE_C_FLAGS "-Wall -Wwrite-strings -Wsign-compare ${CMAKE_C_FLAGS}")
	# set(CMAKE_C_FLAGS "-Wshadow -Werror ${CMAKE_C_FLAGS}")
endif(CMAKE_COMPILER_IS_GNUCC)

# Building Hatari w/o optimization is no fun...
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_C_FLAGS "-O ${CMAKE_C_FLAGS}")
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")

# ####################
# Paths configuration:
# ####################

if(NOT BINDIR)
	set(BINDIR bin)
endif()

if(NOT DATADIR)
	set(DATADIR share/hatari)
endif()

if(NOT MANDIR)
	set(MANDIR share/man/man1)
endif()

if(NOT DOCDIR)
	set(DOCDIR share/doc/hatari)
endif()

# #########################################
# Create config.h and recurse into subdirs:
# #########################################

CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/cmake/config-cmake.h
		${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)
add_subdirectory(doc)
