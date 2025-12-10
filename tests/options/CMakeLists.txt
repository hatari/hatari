
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

# Skip time consuming negative tests (invalid option values
# producing error / non-zero exit values) as those tests need
# to run with Hatari one-by-one, whereas positive (should pass)
# tests can provide Hatari as many CLI options as convenient.

if(ENABLE_TRACING AND Capstone_FOUND)
	add_test(NAME cli-opts-test-success
		COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>
		--skip-negative)
else()
	add_test(NAME cli-opts-test-success
		COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>
		--skip-negative --minimal)
endif()
