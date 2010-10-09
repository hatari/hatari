
include_directories(. ../.. ../includes ${SDL_INCLUDE_DIR}) 

# Unfortunately we've got to specify the rules for the generated files twice,
# once for cross compiling (with calling the host cc directly) and once
# for native compiling so that the rules also work for non-Unix environments...
if(CMAKE_CROSSCOMPILING)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/build68k
		COMMAND cc ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c
		           -o ${CMAKE_CURRENT_BINARY_DIR}/build68k
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build68k.c)

	add_custom_command(OUTPUT cpudefs.c
		COMMAND ./build68k < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/table68k
			${CMAKE_CURRENT_BINARY_DIR}/build68k)

	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		COMMAND cc -I${CMAKE_CURRENT_SOURCE_DIR}
			   cpudefs.c ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			   ${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c
			   -o ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gencpu.c
			${CMAKE_CURRENT_SOURCE_DIR}/readcpu.c cpudefs.c)

	add_custom_command(OUTPUT cpuemu.c cpustbl.c
		COMMAND ${CMAKE_CURRENT_BINARY_DIR}/gencpu
		DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gencpu)

else()	# Rules for normal build follow

	add_executable(build68k build68k.c)

	get_target_property(BUILD68K_EXE build68k LOCATION)
	add_custom_command(OUTPUT cpudefs.c
		COMMAND ${BUILD68K_EXE} < ${CMAKE_CURRENT_SOURCE_DIR}/table68k >cpudefs.c
		DEPENDS table68k build68k)

	add_executable(gencpu gencpu.c readcpu.c cpudefs.c)

	get_target_property(GENCPU_EXE gencpu LOCATION)
	add_custom_command(OUTPUT cpuemu.c cpustbl.c
		COMMAND ${GENCPU_EXE}  DEPENDS gencpu)

endif(CMAKE_CROSSCOMPILING)


# Generated cpuemu.c contains a lot of warnings we don't really care about...
if(CMAKE_COMPILER_IS_GNUCC)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-compare -Wno-shadow")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-variable -Wno-unused-label")
endif(CMAKE_COMPILER_IS_GNUCC)

add_library(UaeCpu
	cpudefs.c cpuemu.c cpustbl.c
	hatari-glue.c memory.c newcpu.c readcpu.c fpp.c
	)
