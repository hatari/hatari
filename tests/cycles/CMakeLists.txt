
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

add_test(NAME cycles-exact
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --compatible false --cpu-exact true)
add_test(NAME cycles-compatible
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --compatible true --cpu-exact false)
