
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(UiRetro OBJECT audio.c disk_control.c joy_ui.c gui_event.c keymap.c main_retro.c
                    microphone.c options.c screen.c statusbar.c timing.c)
target_include_directories(UiRetro PRIVATE ${LIBRETRO_INCLUDE_DIR})
set_target_properties(UiRetro PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
