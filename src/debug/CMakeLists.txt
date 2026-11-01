
if(Readline_FOUND)
	include_directories(${READLINE_INCLUDE_DIR})
endif(Readline_FOUND)

if(ENABLE_DSP_EMU)
	set(DSPDBG_C debugdsp.c)
endif(ENABLE_DSP_EMU)

add_library(Debug OBJECT ${DSPDBG_C}
	    68kDisass.c
	    console.c
	    debugui.c
	    debugInfo.c
	    log.c
	    natfeats.c
	    profile.c
	    profilecpu.c
	    profiledsp.c)

# DebugUT is tested in the unit tests, too, so these files are build seperately
add_library(DebugUT OBJECT
	    breakcond.c
	    debugcpu.c
	    evaluate.c
	    history.c
	    stacktrace.c
	    symbols.c
	    vars.c)

if(ENABLE_LIBRETRO)
	set_target_properties(Debug PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
	set_target_properties(DebugUT PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
endif(ENABLE_LIBRETRO)
