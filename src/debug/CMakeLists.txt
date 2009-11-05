
include_directories(. ../.. ../includes ../uae-cpu ../falcon
		    ${SDL_INCLUDE_DIR}) 

add_library(Debug  log.c debugui.c breakcond.c debugcpu.c evaluate.c)
