
add_subdirectory(debugger)
add_subdirectory(unit)

if(UNIX)
	add_test(NAME command-fifo COMMAND
	         ${CMAKE_CURRENT_SOURCE_DIR}/cmdfifo.sh $<TARGET_FILE:${APP_NAME}>)
	add_test(NAME config-file COMMAND
	         ${CMAKE_CURRENT_SOURCE_DIR}/configfile.sh $<TARGET_FILE:${APP_NAME}>)
	add_subdirectory(blitter)
	add_subdirectory(buserror)
	add_subdirectory(cpu)
	add_subdirectory(cycles)
	add_subdirectory(gemdos)
	add_subdirectory(mem_end)
	add_subdirectory(natfeats)
	add_subdirectory(screen)
	add_subdirectory(serial)
	add_subdirectory(xbios)
endif(UNIX)
