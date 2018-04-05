
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)
get_property(hataribin TARGET hatari PROPERTY LOCATION)

add_test(NAME buserror-st-b COMMAND ${testrunner} ${hataribin} b st)
add_test(NAME buserror-st-w COMMAND ${testrunner} ${hataribin} w st)
add_test(NAME buserror-megast-b COMMAND ${testrunner} ${hataribin} b megast)
add_test(NAME buserror-megast-w COMMAND ${testrunner} ${hataribin} w megast)
add_test(NAME buserror-ste-b COMMAND ${testrunner} ${hataribin} b ste )
add_test(NAME buserror-ste-w COMMAND ${testrunner} ${hataribin} w ste )
