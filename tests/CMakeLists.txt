
add_subdirectory(debugger)

if(UNIX)
	add_subdirectory(buserror)
	add_subdirectory(natfeats)
	add_subdirectory(xbios)
endif(UNIX)
