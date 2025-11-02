
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(Ui audio.c joy_ui.c keymap.c microphone.c screen.c statusbar.c)

target_link_libraries(Ui PRIVATE ${SDL2_LIBRARIES})
