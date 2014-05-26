# This Toolchain file is used to cross compile the Windows 32 bit
# version of Hatari under linux using mingw32
# use : cmake -DCMAKE_TOOLCHAIN_FILE=Toolchain-mingw32-win64_32.cmake


# mingw32 versions of the different tools
# (change these depending on your system settings)
set (MINGW_EXE_PREFIX "i686-w64-mingw32")
set (MINGW_ROOT_PATH "mingw")


#-- Changes should not be required below this point

# The name of the target operating system
SET(CMAKE_SYSTEM_NAME Windows)

# Use the value provided to set mingw's tools
SET(CMAKE_C_COMPILER ${MINGW_EXE_PREFIX}-gcc)
SET(CMAKE_CXX_COMPILER ${MINGW_EXE_PREFIX}-g++)
SET(CMAKE_RC_COMPILER ${MINGW_EXE_PREFIX}-windres)

# Base directory for the target environment
# We use the output from '-print-sysroot'
EXECUTE_PROCESS(
   COMMAND ${CMAKE_C_COMPILER} -print-sysroot
   OUTPUT_VARIABLE CMAKE_FIND_ROOT_PATH
   OUTPUT_STRIP_TRAILING_WHITESPACE
)
# bin/, include/, lib/ and share/ are often in "mingw/"
# You might need to adjust the path for your system
SET(CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH}/${MINGW_ROOT_PATH})

# Make the path absolute, a relative path could confuse some systems
get_filename_component ( CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH} ABSOLUTE )

#message ( "MINGW_ROOT_PATH ${MINGW_ROOT_PATH} MINGW_EXE_PREFIX ${MINGW_EXE_PREFIX} CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH}" )

# FindSDL.cmake doesn't search correctly in CMAKE_FIND_ROOT_PATH
# so we force SDLDIR here
set ( ENV{SDLDIR} ${CMAKE_FIND_ROOT_PATH}/include/SDL )

# Adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

