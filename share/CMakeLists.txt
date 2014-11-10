
foreach(size 32x32 48x48 64x64 128x128 256x256)
	install(FILES icons/hicolor/${size}/apps/hatari.png
		DESTINATION ${ICONDIR}/${size}/apps)
	install(FILES icons/hicolor/${size}/mimetypes/application-x-st-disk-image.png
		      icons/hicolor/${size}/mimetypes/application-x-msa-disk-image.png
		DESTINATION ${ICONDIR}/${size}/mimetypes)
endforeach()

install(FILES icons/hicolor/scalable/apps/hatari.svg
	DESTINATION ${ICONDIR}/scalable/apps)
install(FILES icons/hicolor/scalable/mimetypes/application-x-st-disk-image.svg
	      icons/hicolor/scalable/mimetypes/application-x-msa-disk-image.svg
	DESTINATION ${ICONDIR}/scalable/mimetypes)

install(FILES mime/packages/hatari.xml DESTINATION share/mime/packages)

install(FILES applications/hatari.desktop DESTINATION share/applications)
