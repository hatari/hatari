
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

# Skip time consuming negative tests (invalid option values
# producing error / non-zero exit values) as those need to
# run with Hatari one-by-one, whereas positive (should pass)
# tests can pass Hatari as many CLI options as fits to
# command line.
add_test(NAME cli-opts-test-success
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --skip-negative)
