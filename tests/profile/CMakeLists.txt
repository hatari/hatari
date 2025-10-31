
set(testrunner ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

add_test(NAME profile-disasm-uae
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm uae)
if(HAVE_CAPSTONE_M68K)
add_test(NAME profile-disasm-ext
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm ext)
endif()

add_test(NAME profile-disasm-uae-mmu030
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm uae --mmu on --cpulevel 3)
if(HAVE_CAPSTONE_M68K)
add_test(NAME profile-disasm-ext-mmu030
         COMMAND ${testrunner} $<TARGET_FILE:${APP_NAME}> --disasm ext --mmu on --cpulevel 3)
endif()
