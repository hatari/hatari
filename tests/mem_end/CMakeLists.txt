
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach(machine st ste tt falcon)

  add_test(NAME screen-end-${machine}-color
           COMMAND ${testrunner} $<TARGET_FILE:hatari>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine})

  add_test(NAME screen-end-${machine}-mono
           COMMAND ${testrunner} $<TARGET_FILE:hatari>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine} --mono)

  add_test(NAME screen-end-${machine}-vdi
           COMMAND ${testrunner} $<TARGET_FILE:hatari>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine} --vdi on)

endforeach(machine)
