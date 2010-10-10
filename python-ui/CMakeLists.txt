
INSTALL(PROGRAMS hatariui
	DESTINATION ${BINDIR})

INSTALL(PROGRAMS config.py dialogs.py hatari.py uihelpers.py
	hatariui.py debugui.py
	DESTINATION ${DATADIR}/hatariui/)

INSTALL(FILES README TODO release-notes.txt hatari-icon.png hatari.png
	DESTINATION ${DATADIR}/hatariui/)

INSTALL(FILES hatari-icon.png
	DESTINATION share/icons/hicolor/32x32/apps)

INSTALL(FILES hatariui.desktop
	DESTINATION share/applications)

# if(UNIX)
	add_custom_target(hatariui_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatariui.1 > ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz
		DEPENDS hatariui.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz DESTINATION ${MANDIR})
# endif(UNIX)
