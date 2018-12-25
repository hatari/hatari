
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach(machine st megast ste megaste tt falcon)
 add_test(NAME buserror-${machine}-b COMMAND ${testrunner} $<TARGET_FILE:hatari> b ${machine})
 add_test(NAME buserror-${machine}-w COMMAND ${testrunner} $<TARGET_FILE:hatari> w ${machine})
endforeach(machine)
