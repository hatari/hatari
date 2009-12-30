
include_directories(. ../.. ../includes ${SDL_INCLUDE_DIR}) 

add_custom_command(OUTPUT cpuemu.c cpustbl.c COMMAND ./gencpu  DEPENDS gencpu)

add_custom_command(OUTPUT cpudefs.c
	COMMAND ./build68k < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
	DEPENDS table68k build68k)

if(CMAKE_CROSSCOMPILING)
	add_custom_command(OUTPUT gencpu
		COMMAND cc -I${CMAKE_CURRENT_SOURCE_DIR}
			cpudefs.c ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c -o gencpu
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c cpudefs.c)
	add_custom_command(OUTPUT build68k
		COMMAND cc ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c -o build68k
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c)
else()
	add_executable(gencpu gencpu.c readcpu.c cpudefs.c)
	add_executable(build68k build68k.c)
endif(CMAKE_CROSSCOMPILING)

add_library(UaeCpu
	cpuemu.c cpudefs.c cpustbl.c
	hatari-glue.c memory.c newcpu.c readcpu.c fpp.c
	)
