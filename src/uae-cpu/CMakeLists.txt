
include_directories(. ../.. ../includes ${SDL_INCLUDE_DIR}) 

add_custom_command(OUTPUT cpuemu.c  COMMAND ./gencpu  DEPENDS gencpu)

add_custom_command(OUTPUT cpudefs.c
	COMMAND ./build68k < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
	DEPENDS table68k build68k)

add_executable(gencpu gencpu.c readcpu.c cpudefs.c)

add_executable(build68k build68k.c)

add_library(UaeCpu
	cpuemu.c cpudefs.c cpustbl.c
	hatari-glue.c memory.c newcpu.c readcpu.c fpp.c
	)
