
include_directories(. ../.. ../includes ../uae-cpu ../falcon
		    ${SDL_INCLUDE_DIR}) 

if(ENABLE_DSP_EMU)
	set(DSPDBG_C debugdsp.c)
endif(ENABLE_DSP_EMU)

add_library(Debug
            log.c debugui.c breakcond.c debugcpu.c ${DSPDBG_C} evaluate.c)
