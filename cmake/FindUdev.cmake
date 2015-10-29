
if(UDEV_INCLUDE_DIR)
  # Already in cache, be silent
  set(UDEV_FIND_QUIETLY TRUE)
endif(UDEV_INCLUDE_DIR)

find_path(UDEV_INCLUDE_DIR libudev.h)

find_library(UDEV_LIBRARY NAMES udev)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UDEV DEFAULT_MSG
                                  UDEV_LIBRARY UDEV_INCLUDE_DIR)

mark_as_advanced(UDEV_LIBRARY UDEV_INCLUDE_DIR)
