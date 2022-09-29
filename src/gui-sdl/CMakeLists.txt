
include_directories(. ../.. ../debug ../includes ${SDL2_INCLUDE_DIRS})

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings")
endif()

add_library(GuiSdl
	dlgAbout.c dlgAlert.c dlgCpu.c dlgDevice.c dlgFileSelect.c dlgFloppy.c
	dlgHalt.c dlgHardDisk.c dlgJoystick.c dlgKeyboard.c dlgMain.c
	dlgMemory.c dlgNewDisk.c dlgRom.c dlgScreen.c dlgSound.c dlgSystem.c
	sdlgui.c
	)

target_link_libraries(GuiSdl PRIVATE ${SDL2_LIBRARIES})
