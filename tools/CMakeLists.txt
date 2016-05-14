
add_subdirectory(hmsa)

include(FindPythonInterp)
if(PYTHONINTERP_FOUND)
	add_subdirectory(hconsole)
	add_subdirectory(debugger)
endif(PYTHONINTERP_FOUND)

install(PROGRAMS atari-convert-dir.py DESTINATION ${BINDIR} RENAME atari-convert-dir)
install(PROGRAMS atari-hd-image.sh DESTINATION ${BINDIR} RENAME atari-hd-image)
install(PROGRAMS zip2st.sh DESTINATION ${BINDIR} RENAME zip2st)

if(NOT WIN32)
	add_custom_target(atari_convert_dir_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/atari-convert-dir.1 > ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz
		DEPENDS atari-convert-dir.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz DESTINATION ${MANDIR})

	add_custom_target(atari_hd_image_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/atari-hd-image.1 > ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz
		DEPENDS atari-hd-image.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz DESTINATION ${MANDIR})

	add_custom_target(zip2st_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/zip2st.1 > ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz
		DEPENDS zip2st.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz DESTINATION ${MANDIR})
endif()
