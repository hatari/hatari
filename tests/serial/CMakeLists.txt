
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach(machine st ste tt)
    add_test(NAME serial-mfp-${machine}
             COMMAND ${testrunner} $<TARGET_FILE:hatari> ${machine})
endforeach(machine)
