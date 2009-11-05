
include_directories(. ../.. ../includes ${SDL_INCLUDE_DIR}) 

add_library(GuiSdl
	dlgAbout.c dlgAlert.c dlgDevice.c dlgFileSelect.c dlgFloppy.c
	dlgHardDisk.c dlgJoystick.c dlgKeyboard.c dlgMain.c dlgMemory.c
	dlgNewDisk.c dlgRom.c dlgScreen.c dlgSound.c dlgSystem.c
	sdlgui.c
	)
