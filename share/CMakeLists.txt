
foreach(size 32x32 48x48 64x64 128x128 256x256)
	install(FILES icons/hicolor/${size}/apps/hatari.png
		DESTINATION ${ICONDIR}/${size}/apps)
	install(FILES icons/hicolor/${size}/mimetypes/application-x-st-disk-image.png
		DESTINATION ${ICONDIR}/${size}/mimetypes)
	foreach(type vnd.msa vnd.fastcopy x-stx)
		install(CODE "execute_process(COMMAND ln -sf application-x-st-disk-image.png
				${CMAKE_INSTALL_PREFIX}/${ICONDIR}/${size}/mimetypes/application-${type}-disk-image.png)
				")
	endforeach()
endforeach()

install(FILES icons/hicolor/scalable/apps/hatari.svg
	DESTINATION ${ICONDIR}/scalable/apps)
install(FILES icons/hicolor/scalable/mimetypes/application-x-st-disk-image.svg
	DESTINATION ${ICONDIR}/scalable/mimetypes)
foreach(type vnd.msa vnd.fastcopy x-stx)
	install(CODE "execute_process(COMMAND ln -sf application-x-st-disk-image.svg
			${CMAKE_INSTALL_PREFIX}/${ICONDIR}/scalable/mimetypes/application-${type}-disk-image.svg)
			")
endforeach()
install(FILES mime/packages/hatari.xml DESTINATION share/mime/packages)

install(FILES applications/hatari.desktop DESTINATION share/applications)
