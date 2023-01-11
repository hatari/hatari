
INSTALL(FILES authors.txt bugs.txt emutos.txt keymap-sample.txt
        m68k-linux.txt memory-usage.txt midi-linux.txt
	release-notes.txt scsi-driver.txt thanks.txt todo.txt
	video-recording.txt
	DESTINATION ${DOCDIR})

INSTALL(FILES compatibility.html manual.html debugger.html
        hatari-ui.html manual.css toc.js
	DESTINATION ${DOCDIR})

INSTALL(DIRECTORY images
	DESTINATION ${DOCDIR})

if(ENABLE_MAN_PAGES)
	add_custom_target(manpages ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatari.1
			> ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz
		DEPENDS hatari.1)
	INSTALL(FILES  ${CMAKE_CURRENT_BINARY_DIR}/hatari.1.gz DESTINATION ${MANDIR})
endif(ENABLE_MAN_PAGES)

find_program(TIDY tidy)
if(TIDY)
	add_test(NAME tidy-manual
	         COMMAND tidy -q -e ${CMAKE_CURRENT_SOURCE_DIR}/manual.html)
	add_test(NAME tidy-compatibility
	         COMMAND tidy -q -e ${CMAKE_CURRENT_SOURCE_DIR}/compatibility.html)
	add_test(NAME tidy-debugger
	         COMMAND tidy -q -e ${CMAKE_CURRENT_SOURCE_DIR}/debugger.html)
	add_test(NAME tidy-hatariui
	         COMMAND tidy -q -e ${CMAKE_CURRENT_SOURCE_DIR}/hatari-ui.html)
endif()
