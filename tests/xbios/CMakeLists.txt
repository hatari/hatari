
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

foreach (lvl RANGE 0 4)
 add_test(NAME xbios-680${lvl}0
          COMMAND ${testrunner} $<TARGET_FILE:hatari> --cpulevel ${lvl})
endforeach(lvl)

add_test(NAME xbios-68030-mmu
         COMMAND ${testrunner} $<TARGET_FILE:hatari> --cpulevel 3 --mmu true)
add_test(NAME xbios-68060
         COMMAND ${testrunner} $<TARGET_FILE:hatari> --cpulevel 6 --mmu false)
add_test(NAME xbios-68060-mmu
         COMMAND ${testrunner} $<TARGET_FILE:hatari> --cpulevel 6 --mmu true)
