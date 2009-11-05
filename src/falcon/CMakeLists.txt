
include_directories(. ../.. ../includes ../uae-cpu ../debug
		    ${SDL_INCLUDE_DIR}) 

add_library(Falcon
	crossbar.c dsp.c dsp_core.c dsp_cpu.c dsp_disasm.c
	hostscreen.c microphone.c nvram.c videl.c
	)

