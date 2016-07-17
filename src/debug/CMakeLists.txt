
if(READLINE_FOUND)
	include_directories(${READLINE_INCLUDE_DIR})
endif(READLINE_FOUND)

if(ENABLE_DSP_EMU)
	set(DSPDBG_C debugdsp.c)
endif(ENABLE_DSP_EMU)

add_library(Debug
	    log.c debugui.c breakcond.c debugcpu.c debugInfo.c
	    ${DSPDBG_C} evaluate.c history.c symbols.c vars.c
	    profile.c profilecpu.c profiledsp.c
	    natfeats.c console.c 68kDisass.c)
