
if(MATH_INCLUDE_DIR)
  # Already in cache, be silent
  set(MATH_FIND_QUIETLY TRUE)
endif(MATH_INCLUDE_DIR)

find_path(MATH_INCLUDE_DIR math.h)

find_library(MATH_LIBRARY NAMES m)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Math DEFAULT_MSG
                                  MATH_LIBRARY MATH_INCLUDE_DIR)

mark_as_advanced(MATH_LIBRARY MATH_INCLUDE_DIR)
