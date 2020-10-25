# conftypes.py is created to source directory (instead of build directory)
# so that Hatari UI can be tested directly from the source directory
add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/conftypes.py
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gentypes.py < ${CMAKE_CURRENT_SOURCE_DIR}/../src/configuration.c > ${CMAKE_CURRENT_SOURCE_DIR}/conftypes.py
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/configuration.c ${CMAKE_CURRENT_SOURCE_DIR}/gentypes.py)
add_custom_target(conftypes ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/conftypes.py)

INSTALL(PROGRAMS hatariui
	DESTINATION ${BINDIR})

INSTALL(PROGRAMS hatariui.py debugui.py
	DESTINATION ${DATADIR}/hatariui/)

# Hatari UI Help menu items search for Hatari UI docs from here, so
# changing their target dir would require change also in uihelpers.py
INSTALL(FILES README TODO release-notes.txt hatari-icon.png hatari-logo.png
	config.py dialogs.py hatari.py uihelpers.py
	${CMAKE_CURRENT_SOURCE_DIR}/conftypes.py
	DESTINATION ${DATADIR}/hatariui/)

INSTALL(FILES hatariui.desktop
	DESTINATION share/applications)

if(ENABLE_MAN_PAGES)
	add_custom_target(hatariui_man ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz)
	add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz
		COMMAND gzip -c -9 ${CMAKE_CURRENT_SOURCE_DIR}/hatariui.1 > ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz
		DEPENDS hatariui.1)
	INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hatariui.1.gz DESTINATION ${MANDIR})
endif(ENABLE_MAN_PAGES)
