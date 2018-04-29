
add_subdirectory(debugger)

if(UNIX)
	add_subdirectory(buserror)
	add_subdirectory(natfeats)
	add_subdirectory(xbios)
endif(UNIX)

find_program(CHECKBASHISMS checkbashisms)
find_program(MERCURIAL hg)
execute_process(COMMAND find -name .hg OUTPUT_VARIABLE DOT_HG_DIR
	        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(CHECKBASHISMS AND MERCURIAL AND DOT_HG_DIR)
	 add_test(NAME check-bashisms
	          COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check-bashisms.sh)
endif()
