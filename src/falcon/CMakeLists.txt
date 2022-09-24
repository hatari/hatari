
if(ENABLE_DSP_EMU)
	set(DSP_SOURCES dsp_core.c dsp_cpu.c dsp_disasm.c)
endif(ENABLE_DSP_EMU)

add_library(Falcon
	    crossbar.c microphone.c nvram.c videl.c dsp.c ${DSP_SOURCES})

target_link_libraries(Falcon PRIVATE ${SDL2_LIBRARIES})
