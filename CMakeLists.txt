
cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

if(POLICY CMP0026)
	# Should be removed if cmake_minimum_required >= 2.8.8
	cmake_policy(SET CMP0026 OLD)
endif(POLICY CMP0026)

project(Hatari C)

SET(APP_NAME "Hatari")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckCCompilerFlag)
include(DistClean)

# Set build type to "Release" if user did not specify any build type yet
# Other possible values: Debug, Release, RelWithDebInfo and MinSizeRel
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

# set(CMAKE_VERBOSE_MAKEFILE 1)

# ##########################
# Conditional build features
# ##########################

set(ENABLE_SDL2 0
    CACHE BOOL "Enable building with libSDL2 instead of v1.2")
set(ENABLE_DSP_EMU 1
    CACHE BOOL "Enable DSP 56k emulator for Falcon mode")
set(ENABLE_TRACING 1
    CACHE BOOL "Enable tracing messages for debugging")
set(ENABLE_SMALL_MEM 0
    CACHE BOOL "Enable to use less memory - at the expense of emulation speed")
set(ENABLE_WINUAE_CPU 1
    CACHE BOOL "Enable WinUAE CPU core")

# Run-time checks with GCC "mudflap" etc features:
# - stack protection
# - checking of pointer accesses
#
# Before running CMake, install "mudflap" library development package
# (libmudflap0-4.4-dev in Debian Squeeze and libmudflap-devel in Fedora).
#
# After this you can configure Hatari with Mudflap:
#   cd build; make clean; cmake -D ENABLE_MUDFLAP:BOOL=1 -D ..
# If everything's fine, CMake output should include:
#   Performing Test MUDFLAP_AVAILABLE - Success"
#
# After (re-)building Hatari, run it with something like this:
#   MUDFLAP_OPTIONS="-viol-gdb" src/hatari --sound off --mic off
# (sound&mic are disabled because threading doesn't work well with Mudflap)
#
# For more info:
#   http://gcc.gnu.org/wiki/Mudflap_Pointer_Debugging
#
set(CMAKE_REQUIRED_LIBRARIES "mudflap")
CHECK_C_COMPILER_FLAG("-fmudflapth" MUDFLAP_AVAILABLE)
set(CMAKE_REQUIRED_LIBRARIES "")
if(MUDFLAP_AVAILABLE)
	set(ENABLE_MUDFLAP 0
	    CACHE BOOL "Enable GCC run-time stack/pointer debugging - with huge slowdown")
endif(MUDFLAP_AVAILABLE)

if(APPLE)
	set(ENABLE_OSX_BUNDLE 1
	    CACHE BOOL "Built Hatari as Mac OS X application bundle")
	# set(CMAKE_OSX_ARCHITECTURES "i386" CACHE STRING "Target architectures" FORCE)
	# set(CMAKE_OSX_SYSROOT "/Developer/SDKs/MacOSX10.6.sdk" CACHE STRING "10.6 SDK" FORCE)
	# set(CMAKE_OSX_DEPLOYMENT_TARGET "10.5" CACHE STRING "Target Min 10.5" FORCE)
	set(ADDITIONAL_INCLUDES ${FRAMEWORKS})
	set_source_files_properties(${FRAMEWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
else()
	set(ENABLE_OSX_BUNDLE 0
	    CACHE BOOL "Built Hatari as Mac OS X application bundle")
endif(APPLE)

# ####################
# Check for libraries:
# ####################

if(ENABLE_SDL2)
	find_package(SDL2 REQUIRED)
	if(NOT SDL2_FOUND)
		message(FATAL_ERROR "SDL2 library not found!")
	endif(NOT SDL2_FOUND)
	set(SDL_INCLUDE_DIR ${SDL2_INCLUDE_DIR})
	set(SDL_LIBRARY ${SDL2_LIBRARY})
	set(SDLMAIN_LIBRARY ${SDL2MAIN_LIBRARY})
else(ENABLE_SDL2)
	find_package(SDL REQUIRED)
	if(NOT SDL_FOUND)
		message(FATAL_ERROR "SDL library not found!")
	endif(NOT SDL_FOUND)
endif(ENABLE_SDL2)

find_package(Math)

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

if (NOT ENABLE_OSX_BUNDLE)
	find_package(X11)
	if(X11_FOUND)
		set(HAVE_X11 1)
	endif(X11_FOUND)
endif()

find_package(PortAudio)
if(PORTAUDIO_FOUND)
	set(HAVE_PORTAUDIO 1)
endif(PORTAUDIO_FOUND)

find_package(CapsImage)
if(CAPSIMAGE_FOUND)
	set(HAVE_CAPSIMAGE 1)
endif(CAPSIMAGE_FOUND)

find_package(Udev)
if(UDEV_FOUND)
	set(HAVE_UDEV 1)
endif(UDEV_FOUND)

# ################
# CPP Definitions:
# ################

# Test for large file support:
execute_process(COMMAND getconf LFS_CFLAGS
                OUTPUT_VARIABLE DETECTED_LFS_CFLAGS
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DETECTED_LFS_CFLAGS)
	add_definitions(${DETECTED_LFS_CFLAGS})
	# message(STATUS "Large filesystem flags: ${DETECTED_LFS_CFLAGS}")
endif(DETECTED_LFS_CFLAGS)

# Additional CFLAGS suggested by the SDL library:
if(ENABLE_SDL2)
	add_definitions(-DWITH_SDL2)
	execute_process(COMMAND pkg-config --cflags-only-other sdl2
			OUTPUT_VARIABLE DETECTED_SDL_CFLAGS
			ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
else(ENABLE_SDL2)
	execute_process(COMMAND pkg-config --cflags-only-other sdl
			OUTPUT_VARIABLE DETECTED_SDL_CFLAGS
			ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
endif(ENABLE_SDL2)
if(DETECTED_SDL_CFLAGS)
	add_definitions(${DETECTED_SDL_CFLAGS})
	# message(STATUS "Additional CFLAGS of SDL: ${DETECTED_SDL_CFLAGS}")
endif(DETECTED_SDL_CFLAGS)

if(ENABLE_OSX_BUNDLE)
	# Use OSX native alert windows
	add_definitions(-DALERT_HOOKS=1)
	if(ENABLE_SDL2)
		# We still want to use our SDLMain.m with SDL2
		add_definitions(-DSDL_MAIN_NEEDED=1)
	endif(ENABLE_SDL2)
endif(ENABLE_OSX_BUNDLE)

# ###########################
# Check for optional headers:
# ###########################

check_include_files(termios.h HAVE_TERMIOS_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(malloc.h HAVE_MALLOC_H)
check_include_files(${SDL_INCLUDE_DIR}/SDL_config.h HAVE_SDL_CONFIG_H)
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
check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)
check_function_exists(nanosleep HAVE_NANOSLEEP)
check_function_exists(alphasort HAVE_ALPHASORT)
check_function_exists(scandir HAVE_SCANDIR)
check_function_exists(statvfs HAVE_STATVFS)
check_function_exists(fseeko HAVE_FSEEKO)
check_function_exists(ftello HAVE_FTELLO)
check_function_exists(flock HAVE_FLOCK)
check_function_exists(strlcpy HAVE_LIBC_STRLCPY)
check_struct_has_member("struct dirent" d_type dirent.h HAVE_DIRENT_D_TYPE)

# #############
# Other CFLAGS:
# #############

# GCC pointer debugging, huge run-time slowdown
if(ENABLE_MUDFLAP)
	# SDL mixer threads so have to use threaded mudflap version
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fstack-protector-all -fmudflapth")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fmudflapth -lmudflap")
endif(ENABLE_MUDFLAP)

# Warning flags:
if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-qual -Wbad-function-cast -Wpointer-arith")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes -Wstrict-prototypes")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -Wsign-compare")
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wno-unused-parameter -Wno-empty-body")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow -D_FORTIFY_SOURCE=2 -Werror")
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

if(NOT ETCDIR)
	if(WIN32)
		set(ETCDIR .)
	else()
		set(ETCDIR /etc)
	endif()
endif()

if(NOT ICONDIR)
	set(ICONDIR share/icons/hicolor)
endif()

if(ENABLE_OSX_BUNDLE)
	# put the config files in the app's bundle
	add_definitions(-DCONFDIR=\"../Resources\")
else()
	add_definitions(-DCONFDIR=\"${ETCDIR}\")
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

if(UNIX AND NOT ENABLE_OSX_BUNDLE)
	add_subdirectory(share)
endif()

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/Uninstall.cmake)


# ###################################################################
# Print a summary of the optional libraries with a short explanation:
# ###################################################################

message( "
Libraries summary :
-------------------
")

if(SDL2_FOUND)
  message("  - sdl :\tusing SDL2 v${SDL2_VERSION_STRING}")
else()
  if(SDL_VERSION_STRING)
    message("  - sdl :\tusing SDL v${SDL_VERSION_STRING}")
  else()
    message("  - sdl :\tusing SDL1")
  endif(SDL_VERSION_STRING)
endif(SDL2_FOUND)

if(READLINE_FOUND)
  message( "  - readline :\tfound, enables history/completion in the debugger" )
else()
  message( "  - readline :\tnot found, install it to enable debugger history/completion" )
endif(READLINE_FOUND)

if(ZLIB_FOUND)
  message( "  - zlib :\tfound, allows to use zip/gz files directly" )
else()
  message( "  - zlib :\tnot found, install it to use zip/gz files" )
endif(ZLIB_FOUND)

if(PNG_FOUND)
  message( "  - png :\tfound, allows to compress screenshot/avi files using png" )
else()
  message( "  - png :\tnot found, install it to compress screenshot/avi files using png" )
endif(PNG_FOUND)

if(PORTAUDIO_FOUND)
  message( "  - portaudio :\tfound, enables the microphone input in Falcon mode" )
else()
  message( "  - portaudio :\tnot found, install it to enable the Falcon microphone input" )
endif(PORTAUDIO_FOUND)

if(CAPSIMAGE_FOUND)
  message( "  - capsimage :\tv${CAPSIMAGE_VERSION} found, allow to use .IPF, .RAW and .CTR disk images" )
else()
  message( "  - capsimage :\tv${CAPSIMAGE_VERSION} not found, install it to use .IPF, .RAW and .CTR disk images" )
endif(CAPSIMAGE_FOUND)

if(UDEV_FOUND)
  message( "  - udev :\tfound; allows handling media changing in the NatFeats SCSI Driver on systems using 'udev'" )
else()
  message( "  - udev :\tnot found; needed to handle media changes in the NatFeats SCSI Driver on systems using 'udev'" )
endif(UDEV_FOUND)

message( "" )
