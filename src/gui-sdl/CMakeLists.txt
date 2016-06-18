
include_directories(. ../.. ../debug ../includes ${SDL_INCLUDE_DIR}) 

if(CMAKE_COMPILER_IS_GNUCC)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings")
endif(CMAKE_COMPILER_IS_GNUCC)

add_library(GuiSdl
	dlgAbout.c dlgAlert.c dlgCpu.c dlgDevice.c dlgFileSelect.c dlgFloppy.c
	dlgHalt.c dlgHardDisk.c dlgJoystick.c dlgKeyboard.c dlgMain.c
	dlgMemory.c dlgNewDisk.c dlgRom.c dlgScreen.c dlgSound.c dlgSystem.c
	sdlgui.c
	)
