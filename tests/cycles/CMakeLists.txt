
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

add_test(NAME cycles-exact
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --compatible false --cpu-exact true)
add_test(NAME cycles-compatible
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --compatible true --cpu-exact false)

# add "cyccheck.sym" as target
add_custom_target(cyccheck_sym ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cyccheck.sym)

# generate "cyccheck.sym" from "cyccheck.prg"
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/cyccheck.sym
	COMMAND gst2ascii -b -d ${CMAKE_CURRENT_SOURCE_DIR}/cyccheck.prg > ${CMAKE_CURRENT_SOURCE_DIR}/cyccheck.sym
	DEPENDS gst2ascii ${CMAKE_CURRENT_SOURCE_DIR}/cyccheck.prg)
