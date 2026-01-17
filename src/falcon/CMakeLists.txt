
if(ENABLE_DSP_EMU)
	set(DSP_SOURCES dsp_core.c dsp_cpu.c dsp_disasm.c)
endif(ENABLE_DSP_EMU)

add_library(Falcon OBJECT crossbar.c nvram.c videl.c dsp.c ${DSP_SOURCES})

if(ENABLE_LIBRETRO)
	set_target_properties(Falcon PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
endif(ENABLE_LIBRETRO)
