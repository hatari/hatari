#
# Find the native PORTAUDIO (version 2) includes and library
#
#  PORTAUDIO_INCLUDE_DIR  - where to find portaudio.h, etc.
#  PORTAUDIO_LIBRARY      - List of libraries when using portaudio.
#  PORTAUDIO_FOUND        - True if portaudio found.

include(FindPackageHandleStandardArgs)
include(CheckFunctionExists)

if(PORTAUDIO_INCLUDE_DIR)
  # Already in cache, be silent
  set(PORTAUDIO_FIND_QUIETLY TRUE)
endif(PORTAUDIO_INCLUDE_DIR)

find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)

find_library(PORTAUDIO_LIBRARY NAMES portaudio)

# handle the QUIETLY and REQUIRED arguments and set PORTAUDIO_FOUND to TRUE if 
# all listed variables are TRUE
find_package_handle_standard_args(PORTAUDIO DEFAULT_MSG
                                  PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR)

# Check if it's really a portaudio2 installation...
if(PORTAUDIO_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES ${PORTAUDIO_LIBRARY})
	check_function_exists(Pa_GetDefaultInputDevice HAVE_PA_GETDEFAULTINPUTDEVICE)
	if (NOT HAVE_PA_GETDEFAULTINPUTDEVICE)
		unset (PORTAUDIO_FOUND)
	endif(NOT HAVE_PA_GETDEFAULTINPUTDEVICE)
	set(CMAKE_REQUIRED_LIBRARIES "")
endif(PORTAUDIO_FOUND)

mark_as_advanced(PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR)
