
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

find_program(GM gm)
find_program(IDENTIFY identify)

if(GM OR IDENTIFY)

  add_test(NAME screen-fullscreen-st
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>
                   ${CMAKE_CURRENT_SOURCE_DIR}/flixfull.prg
                   ${CMAKE_CURRENT_SOURCE_DIR}/flix_st.png --machine st)

  add_test(NAME screen-fullscreen-ste
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>
                   ${CMAKE_CURRENT_SOURCE_DIR}/flixfull.prg
                   ${CMAKE_CURRENT_SOURCE_DIR}/flix_ste.png --machine ste)

endif(GM OR IDENTIFY)
