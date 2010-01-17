
INSTALL(PROGRAMS hatari-console.py hatariui
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
