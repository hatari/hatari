
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

	add_custom_command(OUTPUT cpustbl.c cpuemu_0.c cpuemu_11.c cpuemu_13.c
				cpuemu_20.c cpuemu_21.c cpuemu_22.c cpuemu_23.c cpuemu_24.c
				cpuemu_31.c cpuemu_32.c cpuemu_33.c cpuemu_40.c
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
	add_custom_command(OUTPUT cpustbl.c cpuemu_0.c cpuemu_11.c cpuemu_13.c
				cpuemu_20.c cpuemu_21.c cpuemu_22.c cpuemu_23.c cpuemu_24.c
				cpuemu_31.c cpuemu_32.c cpuemu_33.c cpuemu_40.c
		COMMAND ${GENCPU_EXE}  DEPENDS gencpu)

endif(CMAKE_CROSSCOMPILING)


# Generated cpuemu_xx.c contains a lot of warnings we don't really care about...
set(CPUEMU_CFLAGS "-Wno-sign-compare -Wno-shadow -Wno-unused-variable")
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.6)
	set (CPUEMU_CFLAGS "${CPUEMU_CFLAGS} -Wno-unused-but-set-variable")
endif()

if(CMAKE_COMPILER_IS_GNUCC)
	set_source_files_properties( cpuemu_0.c cpuemu_11.c cpuemu_13.c
		cpuemu_20.c cpuemu_21.c cpuemu_22.c cpuemu_23.c cpuemu_24.c
		cpuemu_31.c cpuemu_32.c cpuemu_33.c cpuemu_40.c cpudefs.c cpustbl.c
		PROPERTIES COMPILE_FLAGS ${CPUEMU_CFLAGS})

	# For other files, some warnings can be hidden for release : set next if to "1" instead of "0"
	if ( 1 )
		set(CPUMAIN_CFLAGS "-Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Wno-unused-label -Wno-maybe-uninitialized -Wno-bad-function-cast")
		if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.6)
			set (CPUMAIN_CFLAGS "${CPUMAIN_CFLAGS} -Wno-unused-but-set-variable")
		endif()
		set_source_files_properties( newcpu.c newcpu_common.c cpummu.c fpp.c memory.c
			PROPERTIES COMPILE_FLAGS ${CPUMAIN_CFLAGS})
	endif()

endif(CMAKE_COMPILER_IS_GNUCC)

add_library(UaeCpu
	cpuemu_0.c cpuemu_11.c cpuemu_13.c cpuemu_20.c cpuemu_21.c cpuemu_22.c cpuemu_23.c cpuemu_24.c cpuemu_31.c
	cpuemu_32.c cpuemu_33.c cpuemu_40.c cpudefs.c cpummu.c cpummu030.c cpustbl.c custom.c
	debug.c events.c hatari-glue.c memory.c newcpu_common.c readcpu.c fpp.c newcpu.c
	)
