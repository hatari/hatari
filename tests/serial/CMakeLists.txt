
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach(machine st ste tt)
    add_test(NAME serial-mfp-${machine}
             COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> mfp ${machine})
endforeach(machine)

foreach(machine megaste tt falcon)
    add_test(NAME serial-scc-${machine}
             COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> scc ${machine})
endforeach(machine)

if(NOT PORTMIDI_FOUND)
    foreach(machine st ste tt falcon)
        add_test(NAME serial-midi-${machine}
                 COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> midi ${machine})
    endforeach(machine)
endif(NOT PORTMIDI_FOUND)
