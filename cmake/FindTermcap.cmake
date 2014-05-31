
if(TERMCAP_INCLUDE_DIR)
  # Already in cache, be silent
  set(TERMCAP_FIND_QUIETLY TRUE)
endif(TERMCAP_INCLUDE_DIR)

find_path(TERMCAP_INCLUDE_DIR termcap.h)
find_library(TERMCAP_LIBRARY NAMES termcap)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TERMCAP DEFAULT_MSG
                                  TERMCAP_LIBRARY TERMCAP_INCLUDE_DIR)

mark_as_advanced(TERMCAP_LIBRARY TERMCAP_INCLUDE_DIR)
