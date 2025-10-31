
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

add_test(NAME profile-disasm-uae
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm uae)
if(HAVE_CAPSTONE_M68K)
add_test(NAME profile-disasm-ext
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm ext)
endif()

# TODO: MMU variants of the test
