
# example.py imports hconsole.py, so it needs to be in the same dir
INSTALL(PROGRAMS hconsole.py example.py
	DESTINATION ${DATADIR}/hconsole/)

# files imported by example.py, also need to be in the same dir
INSTALL(FILES example-commands example-debugger
	DESTINATION ${DATADIR}/hconsole/)

# documentation
INSTALL(FILES release-notes.txt
	DESTINATION ${DOCDIR}/hconsole/)

if(ENABLE_MAN_PAGES)
	add_custom_target(hconsole_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hconsole.1 > ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz
		DEPENDS hconsole.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hconsole.1.gz DESTINATION ${MANDIR})
endif(ENABLE_MAN_PAGES)
