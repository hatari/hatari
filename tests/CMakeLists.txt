
add_subdirectory(debugger)

if(UNIX)
	add_subdirectory(buserror)
	add_subdirectory(natfeats)
	add_subdirectory(xbios)
endif(UNIX)

find_program(CHECKBASHISMS checkbashisms)
if(CHECKBASHISMS)
	execute_process(COMMAND find -name "*.sh" OUTPUT_VARIABLE shellscripts
	                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
	string(REGEX REPLACE "\n" ";" shellscripts ${shellscripts})
	foreach(script ${shellscripts})
		string(REGEX REPLACE "\\./tests/" "" scriptname ${script})
		string(REGEX REPLACE "\\./tools/" "" scriptname ${scriptname})
		string(REGEX REPLACE "\\./" "" scriptname ${scriptname})
		string(REGEX REPLACE "/" "-" scriptname ${scriptname})
		string(REGEX REPLACE "\\.sh" "" scriptname ${scriptname})
		add_test(NAME "chksh-${scriptname}"
		         COMMAND ${CHECKBASHISMS} ${CMAKE_SOURCE_DIR}/${script})
	endforeach(script)
endif(CHECKBASHISMS)
