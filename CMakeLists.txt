cmake_minimum_required(VERSION 3.3 FATAL_ERROR)

project(Hatari C)
enable_testing()

set(APP_NAME "Hatari")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckCCompilerFlag)
include(DistClean)
include(CheckTypeSize)

# Set build type to "Release" if user did not specify any build type yet
# Other possible values: Debug, Release, RelWithDebInfo and MinSizeRel
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

# set(CMAKE_VERBOSE_MAKEFILE 1)


######################################################################
# Check if Large File Support is available
# We test if the size of the type 'off_t' is at least 8 bytes
######################################################################

function (check_large_file)
	set (CMAKE_REQUIRED_FLAGS ${HATARI_LFS_FLAGS} )
	check_type_size("off_t" SIZEOF_OFF_T )

	if ( SIZEOF_OFF_T GREATER 7 )
		set ( HATARI_HAVE_LFS 1 CACHE INTERNAL "Large file Support" )
	else()
		set ( HATARI_HAVE_LFS 0 CACHE INTERNAL "Large file Support" )
	endif()
endfunction (check_large_file)



# ##########################
# Conditional build features
# ##########################

set(ENABLE_DSP_EMU 1
    CACHE BOOL "Enable DSP 56k emulator for Falcon mode")
set(ENABLE_TRACING 1
    CACHE BOOL "Enable tracing messages for debugging")
set(ENABLE_SMALL_MEM 0
    CACHE BOOL "Enable to use less memory - at the expense of emulation speed")

# Run-time checks with GCC / LLVM (Clang) AddressSanitizer:
# - stack protection
# - checking of pointer accesses
#
# Configure Hatari with AddressSanitizer:
#   cd build; make clean; cmake -D ENABLE_ASAN:BOOL=1 -D ..
# If everything's fine, CMake output should include:
#   Performing Test ASAN_AVAILABLE - Success"
#
# After (re-)building Hatari, run it as following to see
# available AddressSanitizer options (and that it works):
#   ASAN_OPTIONS=help=true src/hatari
#
# For more info:
#   https://github.com/google/sanitizers/wiki/AddressSanitizer
#
CHECK_C_COMPILER_FLAG("-fsanitize=address" ASAN_AVAILABLE)
if(ASAN_AVAILABLE)
	set(ENABLE_ASAN 0
	    CACHE BOOL "Enable GCC/LLVM run-time stack/pointer debugging (~2x slowdown)")
endif(ASAN_AVAILABLE)

find_program(GZIP gzip)
if(UNIX AND GZIP)
	set(ENABLE_MAN_PAGES 1 CACHE BOOL "Built and install man pages")
else()
	set(ENABLE_MAN_PAGES 0 CACHE BOOL "Built and install man pages")
endif()

if(APPLE)
	set(ENABLE_OSX_BUNDLE 1
	    CACHE BOOL "Built Hatari as macOS application bundle")
	# set(CMAKE_OSX_ARCHITECTURES "i386" CACHE STRING "Target architectures" FORCE)
	# set(CMAKE_OSX_SYSROOT "/Developer/SDKs/MacOSX10.6.sdk" CACHE STRING "10.6 SDK" FORCE)
	# set(CMAKE_OSX_DEPLOYMENT_TARGET "10.5" CACHE STRING "Target Min 10.5" FORCE)
	set(ADDITIONAL_INCLUDES ${FRAMEWORKS})
	set_source_files_properties(${FRAMEWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
else()
	set(ENABLE_OSX_BUNDLE 0
	    CACHE BOOL "Built Hatari as macOS application bundle")
endif(APPLE)

# ####################
# Check for libraries:
# ####################

find_package(SDL2)
if(NOT SDL2_FOUND)
	message(FATAL_ERROR "SDL2 library not found!")
endif(NOT SDL2_FOUND)

find_package(Math)

find_package(Readline)
if(Readline_FOUND)
	set(HAVE_LIBREADLINE 1)
endif(Readline_FOUND)

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
if(PortAudio_FOUND)
	set(HAVE_PORTAUDIO 1)
endif(PortAudio_FOUND)

find_package(PortMidi)
if(PortMidi_FOUND)
	set(HAVE_PORTMIDI 1)
endif(PortMidi_FOUND)

find_package(CapsImage)
if(CapsImage_FOUND)
	set(HAVE_CAPSIMAGE 1)
endif(CapsImage_FOUND)

find_package(Udev)
if(Udev_FOUND)
	set(HAVE_UDEV 1)
endif(Udev_FOUND)

# Check if Large File Support is available with the standard POSIX flags
set (HATARI_LFS_FLAGS "-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64")
check_large_file()


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
execute_process(COMMAND pkg-config --cflags-only-other sdl2
		OUTPUT_VARIABLE DETECTED_SDL_CFLAGS
		ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(DETECTED_SDL_CFLAGS)
	add_definitions(${DETECTED_SDL_CFLAGS})
	# message(STATUS "Additional CFLAGS of SDL: ${DETECTED_SDL_CFLAGS}")
endif(DETECTED_SDL_CFLAGS)

if(ENABLE_OSX_BUNDLE)
	# Use OSX native alert windows
	add_definitions(-DALERT_HOOKS=1)
	# We still want to use our SDLMain.m with SDL2
	add_definitions(-DSDL_MAIN_NEEDED=1)
endif(ENABLE_OSX_BUNDLE)

# ###########################
# Check for optional headers:
# ###########################

check_include_files(termios.h HAVE_TERMIOS_H)
check_include_files(sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(${SDL2_INCLUDE_DIR}/SDL_config.h HAVE_SDL_CONFIG_H)
check_include_files(sys/time.h HAVE_SYS_TIME_H)
check_include_files(sys/times.h HAVE_SYS_TIMES_H)
check_include_files(utime.h HAVE_UTIME_H)
check_include_files(sys/utime.h HAVE_SYS_UTIME_H)
check_include_files("sys/socket.h;sys/un.h" HAVE_UNIX_DOMAIN_SOCKETS)

# #############################
# Check for optional functions:
# #############################

check_symbol_exists(cfmakeraw "termios.h" HAVE_CFMAKERAW)
check_symbol_exists(tcsetattr "termios.h" HAVE_TCSETATTR)
check_symbol_exists(setenv "stdlib.h" HAVE_SETENV)
check_symbol_exists(select "sys/select.h" HAVE_SELECT)
check_symbol_exists(gettimeofday "sys/time.h" HAVE_GETTIMEOFDAY)
check_symbol_exists(nanosleep "time.h" HAVE_NANOSLEEP)
check_symbol_exists(alphasort "dirent.h" HAVE_ALPHASORT)
check_symbol_exists(scandir "dirent.h" HAVE_SCANDIR)
check_symbol_exists(statvfs "sys/statvfs.h" HAVE_STATVFS)
check_symbol_exists(fseeko "stdio.h" HAVE_FSEEKO)
check_symbol_exists(ftello "stdio.h" HAVE_FTELLO)
check_symbol_exists(flock "sys/file.h" HAVE_FLOCK)
check_symbol_exists(strlcpy "string.h" HAVE_LIBC_STRLCPY)
check_struct_has_member("struct dirent" d_type dirent.h HAVE_DIRENT_D_TYPE)

# #############
# Other CFLAGS:
# #############

# GCC/Clang stack/pointer debugging (~2x slowdown)
if(ENABLE_ASAN)
	# better backtraces
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fno-omit-frame-pointer")
	# enable stack protection
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all")
	# enable AddressSanitizer with global variable tracking
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-common")
endif(ENABLE_ASAN)

# GCC/Clang specific flags:
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
	# We want to allow ‘for’-loop initial declarations a la for(int i=0; ...)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
	# Warning flags:
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-qual -Wbad-function-cast -Wpointer-arith")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes -Wstrict-prototypes")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -Wsign-compare")
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wno-unused-parameter -Wno-empty-body")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-security")
	#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow -D_FORTIFY_SOURCE=2")
endif()

if(ENABLE_WERROR)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
endif(ENABLE_WERROR)

# Building Hatari w/o optimization is no fun...
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_C_FLAGS "-O ${CMAKE_C_FLAGS}")
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")

# Always add the Large File Support flags in case they are supported
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${HATARI_LFS_FLAGS}")


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
add_subdirectory(tests)
add_subdirectory(tools)

include(FindPythonInterp)
if(PYTHONINTERP_FOUND AND PYTHON_VERSION_MAJOR LESS 3)
	message("Note: Hatari needs at least Python 3 ... ignoring older version")
	unset(PYTHONINTERP_FOUND)
endif()
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

message("  - sdl :\tusing SDL2 v${SDL2_VERSION_STRING}")

if(Readline_FOUND)
  message( "  - readline :\tfound, enables history/completion in the debugger" )
else()
  message( "  - readline :\tnot found, install it to enable debugger history/completion" )
endif(Readline_FOUND)

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

if(PortAudio_FOUND)
  message( "  - portaudio :\tfound, enables the microphone input in Falcon mode" )
else()
  message( "  - portaudio :\tnot found, install it to enable the Falcon microphone input" )
endif(PortAudio_FOUND)

if(PortMidi_FOUND)
  message( "  - portmidi :\tfound, required for (non-Linux) MIDI support" )
else()
  message( "  - portmidi :\tnot found, install it for MIDI support on Windows / OSX" )
endif(PortMidi_FOUND)

if(CapsImage_FOUND)
  message( "  - capsimage :\tv5 found, allow to use .IPF, .RAW and .CTR disk images" )
else()
  message( "  - capsimage :\tv5 not found, install it to use .IPF, .RAW and .CTR disk images" )
endif(CapsImage_FOUND)

if(Udev_FOUND)
  message( "  - udev :\tfound, required for media change detection in NatFeats SCSI" )
  message( "          \tdevices on udev-based systems (Linux)" )
else()
  if(UNIX AND NOT APPLE AND NOT CYGWIN)
    message( "  - udev :\tnot found, install it to enable media change detection in" )
    message( "          \tNatFeats SCSI devices on udev-based systems (Linux)" )
  endif()
endif(Udev_FOUND)

if (HATARI_HAVE_LFS)
	message("  - LFS :\tLarge File Support is available (size of off_t = ${SIZEOF_OFF_T}),")
	message("         \tAVI recording and HD image files can be bigger than 2 GB")
else()
	message("  - LFS :\tLarge File Support is NOT available (size of off_t = ${SIZEOF_OFF_T}),")
	message("         \tAVI recording and HD image files will be limited to 2 GB")
endif()

if(NOT HAVE_SYS_TIMES_H)
  message("\n  Note: times() function is missing (sys/times.h is not available)")
  message("        ==> using inaccurate SDL_GetTicks() instead")
endif()

message( "" )
