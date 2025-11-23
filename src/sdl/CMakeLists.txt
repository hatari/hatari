
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(Ui audio.c joy_ui.c keymap.c microphone.c screen.c statusbar.c)

target_compile_definitions(Ui PRIVATE SDL_ENABLE_OLD_NAMES)
target_link_libraries(Ui PRIVATE ${SDL2_LIBRARIES})
