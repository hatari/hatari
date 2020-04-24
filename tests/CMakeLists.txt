
add_subdirectory(debugger)

if(UNIX)
	add_test(NAME config-file COMMAND
	         ${CMAKE_CURRENT_SOURCE_DIR}/configfile.sh $<TARGET_FILE:hatari>)
	add_subdirectory(buserror)
	add_subdirectory(cpu)
	add_subdirectory(cycles)
	add_subdirectory(gemdos)
	add_subdirectory(natfeats)
	add_subdirectory(serial)
	add_subdirectory(xbios)
endif(UNIX)
