
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach (lvl 0 3 4)
 add_test(NAME cpu-integer-680${lvl}0
          COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}>
                  ${CMAKE_CURRENT_SOURCE_DIR}/int_test.tos --cpulevel ${lvl})
endforeach(lvl)
