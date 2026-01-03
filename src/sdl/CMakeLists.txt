
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(Ui audio.c joy_ui.c gui_event.c keymap.c main_sdl.c microphone.c
               screen.c statusbar.c )

target_compile_definitions(Ui PRIVATE SDL_ENABLE_OLD_NAMES)
target_include_directories(Ui PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(Ui PRIVATE ${SDL2_LIBRARIES})
