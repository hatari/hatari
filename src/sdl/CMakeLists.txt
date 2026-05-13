
include_directories(. ../.. ../includes ../debug ../falcon)

add_library(Ui audio.c joy_ui.c gui_event.c keymap.c main_sdl.c microphone.c
               screen.c statusbar.c timing.c)

target_compile_definitions(Ui PRIVATE SDL_ENABLE_OLD_NAMES)
if(ENABLE_METAL_RENDERER)
	target_compile_definitions(Ui PRIVATE ENABLE_METAL_RENDERER=1)
endif()
target_include_directories(Ui PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(Ui PRIVATE ${SDL2_LIBRARIES})
