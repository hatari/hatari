
# include_directories(../includes ${SDL_INCLUDE_DIR}) 

add_library(GuiOsx
	AlertHooks.m PrefsController.m Shared.m
	CreateFloppyController.m SDLMain.m)
