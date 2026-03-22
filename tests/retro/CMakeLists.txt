
add_executable(test-retro test-retro.c)
target_include_directories(test-retro PRIVATE ${LIBRETRO_INCLUDE_DIR})

add_test(NAME retro COMMAND test-retro $<TARGET_FILE:retro-hatari>)
