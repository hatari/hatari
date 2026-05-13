
add_subdirectory(hmsa)

if(HATARI_ENABLE_PYTHON_TOOLS)
	include(FindPython)
	if(Python_Interpreter_FOUND)
		add_subdirectory(hconsole)
		add_subdirectory(debugger)
	endif(Python_Interpreter_FOUND)
endif(HATARI_ENABLE_PYTHON_TOOLS)

install(PROGRAMS hatari-prg-args.sh DESTINATION ${BINDIR} RENAME hatari-prg-args)
install(PROGRAMS zip2st.sh DESTINATION ${BINDIR} RENAME zip2st)

if(HATARI_ENABLE_PYTHON_TOOLS)
	install(PROGRAMS atari-convert-dir.py DESTINATION ${BINDIR} RENAME atari-convert-dir)
	install(PROGRAMS atari-hd-image.sh DESTINATION ${BINDIR} RENAME atari-hd-image)
endif(HATARI_ENABLE_PYTHON_TOOLS)

if(ENABLE_MAN_PAGES)
	if(HATARI_ENABLE_PYTHON_TOOLS)
		add_custom_target(atari_convert_dir_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz)
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz
			COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/atari-convert-dir.1 > ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz
			DEPENDS atari-convert-dir.1)
		INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/atari-convert-dir.1.gz DESTINATION ${MANDIR})
	endif(HATARI_ENABLE_PYTHON_TOOLS)

	add_custom_target(hatari_prg_args_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatari-prg-args.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatari-prg-args.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatari-prg-args.1 > ${CMAKE_CURRENT_BINARY_DIR}/hatari-prg-args.1.gz
		DEPENDS hatari-prg-args.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hatari-prg-args.1.gz DESTINATION ${MANDIR})

	if(HATARI_ENABLE_PYTHON_TOOLS)
		add_custom_target(atari_hd_image_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz)
		add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz
			COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/atari-hd-image.1 > ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz
			DEPENDS atari-hd-image.1)
		INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/atari-hd-image.1.gz DESTINATION ${MANDIR})
	endif(HATARI_ENABLE_PYTHON_TOOLS)

	add_custom_target(zip2st_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/zip2st.1 > ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz
		DEPENDS zip2st.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/zip2st.1.gz DESTINATION ${MANDIR})
endif(ENABLE_MAN_PAGES)
