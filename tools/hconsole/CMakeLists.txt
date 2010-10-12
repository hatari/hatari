
INSTALL(PROGRAMS hconsole.py example.py
	DESTINATION ${DATADIR}/hconsole/)

# files related to example.py
INSTALL(FILES example-commands example-debugger
	DESTINATION ${DATADIR}/hconsole/)

# documentation
INSTALL(FILES release-notes.txt
	DESTINATION ${DATADIR}/hconsole/)

# if(UNIX)
	add_custom_target(hconsole_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hconsole.1 > ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz
		DEPENDS hconsole.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz DESTINATION ${MANDIR})
# endif(UNIX)
