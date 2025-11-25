
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

add_test(NAME cli-options COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>)
