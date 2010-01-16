#
# Find the native PORTAUDIO includes and library
#
#  PORTAUDIO_INCLUDE_DIR  - where to find portaudio.h, etc.
#  PORTAUDIO_LIBRARY      - List of libraries when using portaudio.
#  PORTAUDIO_FOUND        - True if portaudio found.
#

IF (PORTAUDIO_INCLUDE_DIR)
  # Already in cache, be silent
  SET(PORTAUDIO_FIND_QUIETLY TRUE)
ENDIF (PORTAUDIO_INCLUDE_DIR)

FIND_PATH(PORTAUDIO_INCLUDE_DIR portaudio.h)

FIND_LIBRARY(PORTAUDIO_LIBRARY NAMES portaudio)

# handle the QUIETLY and REQUIRED arguments and set PORTAUDIO_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(PORTAUDIO DEFAULT_MSG
                                  PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR)

MARK_AS_ADVANCED(PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR)
