
INSTALL(FILES authors.txt emutos.txt keymap-sample.txt memory-usage.txt
	midi-linux.txt release-notes.txt todo.txt
	DESTINATION ${DOCDIR})

INSTALL(FILES compatibility.html  manual.html
	DESTINATION ${DOCDIR})

INSTALL(DIRECTORY images
	DESTINATION ${DOCDIR})

if(NOT WIN32)
	add_custom_target(manpages ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatari.1 > ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz
		DEPENDS hatari.1)
	INSTALL(FILES  ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz DESTINATION ${MANDIR})
endif()
