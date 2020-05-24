
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

if(ENABLE_SDL2)

 find_program(GM gm)
 find_program(IDENTIFY identify)

 if(GM OR IDENTIFY)

  add_test(NAME screen-fullscreen
           COMMAND ${testrunner} $<TARGET_FILE:hatari>
                   ${CMAKE_CURRENT_SOURCE_DIR}/flixfull.prg
                   ${CMAKE_CURRENT_SOURCE_DIR}/flixref.png)

 endif(GM OR IDENTIFY)

endif(ENABLE_SDL2)
