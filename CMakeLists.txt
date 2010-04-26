
cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

project(Hatari)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckIncludeFiles)
include(CheckFunctionExists)

include(DistClean)

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

if(APPLE)
	set(ENABLE_OSX_BUNDLE 1
	    CACHE BOOL "Built Hatari as Mac OS X application bundle")
else()
	set(ENABLE_OSX_BUNDLE 0
	    CACHE BOOL "Built Hatari as Mac OS X application bundle")
endif(APPLE)

# ####################
# Check for libraries:
# ####################

find_package(SDL REQUIRED)
if(NOT SDL_FOUND)
	message(FATAL_ERROR "SDL library not found!")
endif(NOT SDL_FOUND)

find_package(Math)

find_package(Readline)
if(READLINE_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES "readline")
	check_function_exists(rl_filename_completion_function HAVE_RL_COMPLETION_FUNCTION)
	if(HAVE_RL_COMPLETION_FUNCTION)
		set(HAVE_LIBREADLINE 1)
	endif(HAVE_RL_COMPLETION_FUNCTION)
	set(CMAKE_REQUIRED_LIBRARIES "")
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

# ################
# CPP Definitions:
# ################

add_definitions(-DCONFDIR=\"/etc\")

# Test for large file support:
execute_process(COMMAND getconf LFS_CFLAGS
                OUTPUT_VARIABLE DETECTED_LFS_CFLAGS
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DETECTED_LFS_CFLAGS)
	add_definitions(${DETECTED_LFS_CFLAGS})
	# message(STATUS "Large filesystem flags: ${DETECTED_LFS_CFLAGS}")
endif(DETECTED_LFS_CFLAGS)

# Additional CFLAGS suggested by the SDL library:
execute_process(COMMAND pkg-config --cflags-only-other sdl
                OUTPUT_VARIABLE DETECTED_SDL_CFLAGS
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DETECTED_SDL_CFLAGS)
	add_definitions(${DETECTED_SDL_CFLAGS})
	# message(STATUS "Additional CFLAGS of SDL: ${DETECTED_SDL_CFLAGS}")
endif(DETECTED_SDL_CFLAGS)

# ###########################
# Check for optional headers:
# ###########################

check_include_files(termios.h HAVE_TERMIOS_H)
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
check_function_exists(posix_memalign HAVE_POSIX_MEMALIGN)
check_function_exists(memalign HAVE_MEMALIGN)

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

if(NOT BIN2DATADIR)
	if(WIN32)
		set(BIN2DATADIR "."
		    CACHE STRING "Relative path from bindir to datadir")
	elseif(ENABLE_OSX_BUNDLE)
		set(BIN2DATADIR "../Resources"
		    CACHE STRING "Relative path from bindir to datadir")
	else()
		set(BIN2DATADIR "../share/hatari"
		    CACHE STRING "Relative path from bindir to datadir")
	endif(WIN32)
	mark_as_advanced(BIN2DATADIR)
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

configure_file(${CMAKE_SOURCE_DIR}/cmake/config-cmake.h
		${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(src)
add_subdirectory(doc)
add_subdirectory(tools)

include(FindPythonInterp)
if(PYTHONINTERP_FOUND)
	add_subdirectory(python-ui)
endif(PYTHONINTERP_FOUND)
