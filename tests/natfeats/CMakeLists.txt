
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach (lvl RANGE 0 4)
  add_test(NAME natfeats-680${lvl}0-fast
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel ${lvl} --compatible false --cpu-exact false)
  add_test(NAME natfeats-680${lvl}0-compatible
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel ${lvl} --compatible true --cpu-exact false)
  add_test(NAME natfeats-680${lvl}0-cpuexact
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel ${lvl} --compatible false --cpu-exact true)
  if (${lvl} GREATER 2)
  add_test(NAME natfeats-680${lvl}0-mmu
           COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel ${lvl} --mmu true)
  endif()
endforeach(lvl)

add_test(NAME natfeats-68060
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel 6 --mmu false)
add_test(NAME natfeats-68060-mmu
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --cpulevel 6 --mmu true)
