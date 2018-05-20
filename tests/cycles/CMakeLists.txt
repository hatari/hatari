
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

if(ENABLE_WINUAE_CPU)
 add_test(NAME cycles-exact
          COMMAND ${testrunner} $<TARGET_FILE:hatari> --compatible false --cpu-exact true)
 add_test(NAME cycles-compatible
          COMMAND ${testrunner} $<TARGET_FILE:hatari> --compatible true --cpu-exact false)
else(ENABLE_WINUAE_CPU)
 add_test(NAME cycles-compatible
          COMMAND ${testrunner} $<TARGET_FILE:hatari> --compatible true)
endif(ENABLE_WINUAE_CPU)
