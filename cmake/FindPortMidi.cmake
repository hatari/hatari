#
# Find the native PORTMIDI includes and library
#
#  PORTMIDI_INCLUDE_DIR  - where to find portmidi.h, etc.
#  PORTMIDI_LIBRARY      - List of libraries when using portmidi.
#  PORTMIDI_FOUND       - True if portmidi found.

include(FindPackageHandleStandardArgs)
include(CheckFunctionExists)

if(PORTMIDI_INCLUDE_DIR)
  # Already in cache, be silent
  set(PORTMIDI_FIND_QUIETLY TRUE)
endif(PORTMIDI_INCLUDE_DIR)

find_path(PORTMIDI_INCLUDE_DIR portmidi.h)

find_library(PORTMIDI_LIBRARY NAMES portmidi)

# handle the QUIETLY and REQUIRED arguments and set PORTMIDI_FOUND to TRUE if 
# all listed variables are TRUE
find_package_handle_standard_args(PORTMIDI DEFAULT_MSG
                                  PORTMIDI_LIBRARY PORTMIDI_INCLUDE_DIR)

# Check if it's really a portmidi installation...
if(PORTMIDI_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES ${PORTMIDI_LIBRARY})
	check_function_exists(Pm_Initialize HAVE_PM_INITIALIZE)
	if (NOT HAVE_PM_INITIALIZE)
		unset (PORTMIDI_FOUND)
	endif(NOT HAVE_PM_INITIALIZE)
	set(CMAKE_REQUIRED_LIBRARIES "")
endif(PORTMIDI_FOUND)

mark_as_advanced(PORTMIDI_LIBRARY PORTMIDI_INCLUDE_DIR)
