
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(UiRetro OBJECT audio.c joy_ui.c gui_event.c keymap.c main_retro.c
                    microphone.c screen.c statusbar.c timing.c)
set_target_properties(UiRetro PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
