
include_directories(. ../.. ../includes ../uae-cpu ../debug
		    ${SDL_INCLUDE_DIR}) 

if(ENABLE_DSP_EMU)
	set(DSP_SOURCES dsp_core.c dsp_cpu.c dsp_disasm.c)
endif(ENABLE_DSP_EMU)

add_library(Falcon
	crossbar.c dsp.c ${DSP_SOURCES}
	hostscreen.c microphone.c nvram.c videl.c
	)
