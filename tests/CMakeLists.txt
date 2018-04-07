
add_subdirectory(debugger)

if(UNIX)
	add_subdirectory(buserror)
	add_subdirectory(natfeats)
endif(UNIX)
