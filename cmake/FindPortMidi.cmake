#
# Find the native PortMidi includes and library
#
#  PORTMIDI_INCLUDE_DIR  - where to find portmidi.h, etc.
#  PORTMIDI_LIBRARY      - List of libraries when using portmidi.
#  PortMidi_FOUND       - True if portmidi found.

include(FindPackageHandleStandardArgs)
include(CheckSymbolExists)

if(PORTMIDI_INCLUDE_DIR)
  # Already in cache, be silent
  set(PORTMIDI_FIND_QUIETLY TRUE)
endif(PORTMIDI_INCLUDE_DIR)

find_path(PORTMIDI_INCLUDE_DIR portmidi.h)

find_library(PORTMIDI_LIBRARY NAMES portmidi)

# handle the QUIETLY and REQUIRED arguments and set PortMidi_FOUND to TRUE if 
# all listed variables are TRUE
find_package_handle_standard_args(PortMidi DEFAULT_MSG
                                  PORTMIDI_LIBRARY PORTMIDI_INCLUDE_DIR)

# Check if it's really a portmidi installation...
if(PortMidi_FOUND)
	set(CMAKE_REQUIRED_LIBRARIES ${PORTMIDI_LIBRARY})
	set(CMAKE_REQUIRED_INCLUDES ${PORTMIDI_INCLUDE_DIR})
	check_symbol_exists(Pm_Initialize "portmidi.h" HAVE_PM_INITIALIZE)
	if (NOT HAVE_PM_INITIALIZE)
		unset (PortMidi_FOUND)
	endif(NOT HAVE_PM_INITIALIZE)
	set(CMAKE_REQUIRED_LIBRARIES "")
	set(CMAKE_REQUIRED_INCLUDES "")
endif(PortMidi_FOUND)

mark_as_advanced(PORTMIDI_LIBRARY PORTMIDI_INCLUDE_DIR)
