# This Toolchain file is used to cross compile the windows
# version of Hatari under linux using mingw32
# use : cmake -DCMAKE_TOOLCHAIN_FILE=Toolchain-mingw32.cmake

# The name of the target operating system
SET(CMAKE_SYSTEM_NAME Windows)

# mingw32 versions of the different tools
# (change these depending on your system settings)
SET(CMAKE_C_COMPILER i586-pc-mingw32-gcc)
SET(CMAKE_CXX_COMPILER i586-pc-mingw32-g++)
SET(CMAKE_RC_COMPILER i586-pc-mingw32-windres)

# Base directory for the target environment
SET(CMAKE_FIND_ROOT_PATH /usr/i586-pc-mingw32/sys-root/mingw )

# FindSDL.cmake doesn't search correctly in CMAKE_FIND_ROOT_PATH
# so we force SDLDIR here
set ( ENV{SDLDIR} ${CMAKE_FIND_ROOT_PATH}/include/SDL )

# Adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search 
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

