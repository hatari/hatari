
set(scr_runner ${CMAKE_CURRENT_SOURCE_DIR}/scr_run_test.sh)

foreach(machine st ste tt falcon)

  add_test(NAME screen-end-${machine}-color
           COMMAND ${scr_runner} $<TARGET_FILE:${APP_NAME}>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine})

  add_test(NAME screen-end-${machine}-mono
           COMMAND ${scr_runner} $<TARGET_FILE:${APP_NAME}>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine} --mono)

  add_test(NAME screen-end-${machine}-vdi
           COMMAND ${scr_runner} $<TARGET_FILE:${APP_NAME}>
                   ${CMAKE_CURRENT_SOURCE_DIR}/scr_end.prg
                       --machine ${machine} --vdi on)

endforeach(machine)


set(dsnd_runner ${CMAKE_CURRENT_SOURCE_DIR}/dsnd_run_test.sh)

add_test(NAME dma-sound-end-ste
         COMMAND ${dsnd_runner} $<TARGET_FILE:${APP_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/dsnd_end.prg
                     --machine ste)

add_test(NAME dma-sound-end-falcon
         COMMAND ${dsnd_runner} $<TARGET_FILE:${APP_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/dsnd_end.prg
                     --machine falcon --cpuclock 8)
