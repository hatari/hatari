# This Toolchain file is used to cross compile the windows
# version of Hatari under linux using mingw32
# use : cmake -DCMAKE_TOOLCHAIN_FILE=Toolchain-mingw32.cmake
#
# If MINGW_EXE_PREFIX is defined, we use this to set the name
# for gcc/g++/windres required for cross compilation
#
# If MINGW_ROOT_PATH is defined, we use this to build the full
# path used when looking for lib/ and include/ in mingw's sysroot/


# The name of the target operating system
SET(CMAKE_SYSTEM_NAME Windows)

# mingw32 versions of the different tools
# (change these depending on your system settings)
IF(NOT MINGW_EXE_PREFIX)
  # Some default values if MINGW_EXE_PREFIX is not defined
  SET(CMAKE_C_COMPILER i586-pc-mingw32-gcc)
  SET(CMAKE_CXX_COMPILER i586-pc-mingw32-g++)
  SET(CMAKE_RC_COMPILER i586-pc-mingw32-windres)
ELSE()
  # Use the value provided to set mingw's tools
  SET(CMAKE_C_COMPILER ${MINGW_EXE_PREFIX}-gcc)
  SET(CMAKE_CXX_COMPILER ${MINGW_EXE_PREFIX}-g++)
  SET(CMAKE_RC_COMPILER ${MINGW_EXE_PREFIX}-windres)
ENDIF()

# Base directory for the target environment
# We use the output from '-print-sysroot'
EXECUTE_PROCESS( 
   COMMAND ${CMAKE_C_COMPILER} -print-sysroot
   OUTPUT_VARIABLE CMAKE_FIND_ROOT_PATH
   OUTPUT_STRIP_TRAILING_WHITESPACE
)
# bin/, include/, lib/ and share/ are often in "mingw/"
# You might need to adjust the path for your system
IF(NOT MINGW_ROOT_PATH)
  # Default value if MINGW_ROOT_PATH is not defined
  SET(CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH}/mingw)
ELSE()
  # Use the value passed to cmake
  SET(CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH}/${MINGW_ROOT_PATH})
ENDIF()



# Make the path absolute, a relative path could confuse some systems
get_filename_component ( CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH} ABSOLUTE )

# Uncomment this line with your own values if above doesn't work
#SET(CMAKE_FIND_ROOT_PATH /usr/i586-pc-mingw32/sys-root/mingw )

# FindSDL.cmake doesn't search correctly in CMAKE_FIND_ROOT_PATH
# so we force SDLDIR here
set ( ENV{SDLDIR} ${CMAKE_FIND_ROOT_PATH}/include/SDL )

# Adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search 
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

