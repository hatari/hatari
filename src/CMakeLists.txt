
set(SOURCES
	acia.c audio.c avi_record.c bios.c blitter.c cart.c cfgopts.c
	clocks_timings.c configuration.c options.c change.c control.c
	cycInt.c cycles.c dialog.c dmaSnd.c fdc.c file.c floppy.c
	floppy_ipf.c floppy_stx.c gemdos.c hdc.c ide.c ikbd.c
	ioMem.c ioMemTabST.c ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c
	keymap.c m68000.c main.c midi.c memorySnapShot.c mfp.c nf_scsidrv.c
	ncr5380.c paths.c  psg.c printer.c resolution.c rs232.c reset.c rtc.c
	scandir.c scc.c stMemory.c screen.c screenConvert.c screenSnapShot.c
	shortcut.c sound.c spec512.c statusbar.c str.c tos.c utils.c
	vdi.c vme.c inffile.c video.c wavFormat.c xbios.c ymFormat.c lilo.c)

if(EMSCRIPTEN)
	set(SOURCES ${SOURCES} emscripten.js emscripten_shell.html)
endif()

# Disk image code is shared with the hmsa tool, so we put it into a library:
add_library(Floppy createBlankImage.c dim.c msa.c st.c zip.c utils.c)

# When building for macOS, define specific sources for gui and resources
if(ENABLE_OSX_BUNDLE)
	set(GUIOSX_SOURCES
	    gui-osx/AlertHooks.m gui-osx/PrefsController.m gui-osx/Shared.m
	    gui-osx/CreateFloppyController.m gui-osx/SDLMain.m gui-osx/paths.m)
	set_source_files_properties(${GUIOSX_SOURCES} PROPERTIES LANGUAGE C)
	set(GUIOSX_RSRCS
	    Hatari.icns gui-osx/stdisk.png gui-osx/en.lproj gui-osx/fr.lproj)
	set(GUIOSX_DOCS
	    ${CMAKE_SOURCE_DIR}/doc/manual.html ${CMAKE_SOURCE_DIR}/doc/images
	    ${CMAKE_SOURCE_DIR}/doc/compatibility.html ${CMAKE_SOURCE_DIR}/doc/toc.js )
	# these are the macOS Interface Builder Files
	set (HATARI_XIBS en.lproj/SDLMain fr.lproj/SDLMain)
elseif(APPLE)
	set(SOURCES ${SOURCES} gui-osx/paths.m)
endif()

# When building for Windows, define specific sources for gui and resources
# and set the subsystem of the resulting .exe to "windows GUI" instead of "console"
# Recent mingw version sets _FORTIFY_SOURCE, which requires to link with lib ssp.
# We use "--as-needed" to keep compatibility with older mingw that don't require lib ssp
if(WIN32)
	set(GUIWIN_SOURCES gui-win/opencon.c)
	set(GUIWIN_RES gui-win/hatari-winicon.rc)
	if(CMAKE_COMPILER_IS_GNUCC)
		set(CMAKE_EXE_LINKER_FLAGS
		    "${CMAKE_EXE_LINKER_FLAGS} -mwindows -Wl,--as-needed -lssp")
	endif(CMAKE_COMPILER_IS_GNUCC)
endif(WIN32)

include_directories(includes debug falcon
		    ${CMAKE_BINARY_DIR} ${SDL2_INCLUDE_DIRS} cpu)

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
	set(SOURCES ${SOURCES} unzip.c)
endif(ZLIB_FOUND)

if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIR})
endif(PNG_FOUND)

if(PortMidi_FOUND)
	include_directories(${PORTMIDI_INCLUDE_DIR})
endif(PortMidi_FOUND)

if(X11_FOUND)
	include_directories(${X11_INCLUDE_DIR})
endif(X11_FOUND)

if(CapsImage_FOUND)
	include_directories(${CAPSIMAGE_INCLUDE_DIR})
endif(CapsImage_FOUND)

if(Capstone_FOUND)
	include_directories(${CAPSTONE_INCLUDE_DIR})
endif(Capstone_FOUND)

link_directories(${CMAKE_CURRENT_BINARY_DIR}/debug
		 ${CMAKE_CURRENT_BINARY_DIR}/falcon
		 ${CMAKE_CURRENT_BINARY_DIR}/gui-sdl
		 ${CMAKE_CURRENT_BINARY_DIR}/cpu)

add_subdirectory(debug)
add_subdirectory(falcon)
add_subdirectory(gui-sdl)
add_subdirectory(cpu)

# When building for macOS, add specific sources
if(ENABLE_OSX_BUNDLE)
	set_source_files_properties(${FRAMEWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
	add_executable(${APP_NAME} MACOSX_BUNDLE ${GUIOSX_RSRCS} ${GUIOSX_DOCS} ${SOURCES} ${GUIOSX_SOURCES} ${FRAMEWORKS})
	set_target_properties(${APP_NAME} PROPERTIES MACOSX_BUNDLE_INFO_PLIST
			      ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/Info-Hatari.plist)
	set_target_properties(${APP_NAME} PROPERTIES INSTALL_RPATH
			      "@executable_path/../Frameworks")
	set_target_properties(${APP_NAME} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
	set(MACOSX_BUNDLE_ICON_FILE Hatari.icns)
	set(MACOSX_BUNDLE_GUI_IDENTIFIER org.tuxfamily.Hatari)
	if(CMAKE_GENERATOR MATCHES "Xcode")
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/\${CONFIGURATION}/${APP_NAME}.app/Contents)
	else()
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.app/Contents)
	endif()
	if(PNG_FOUND)
		set(COPY_PNG cp -R ${PNG_LIBRARY} ${BUNDLE_CONTENTS}/Frameworks/)
	endif()
	# Create the Hatari.icns file
	set(ICONSDIR ${CMAKE_SOURCE_DIR}/share/icons/hicolor/)
	add_custom_command(OUTPUT ${MACOSX_BUNDLE_ICON_FILE}
		COMMAND mkdir -p ${CMAKE_BINARY_DIR}/Hatari.iconset
		COMMAND sips -Z 16 ${ICONSDIR}/32x32/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_16x16.png
		COMMAND sips -Z 32 ${ICONSDIR}/32x32/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_16x16@2x.png
		COMMAND sips -Z 32 ${ICONSDIR}/32x32/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_32x32.png
		COMMAND sips -Z 64 ${ICONSDIR}/64x64/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_32x32@2x.png
		COMMAND sips -Z 128 ${ICONSDIR}/128x128/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_128x128.png
		COMMAND sips -Z 256 ${ICONSDIR}/256x256/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_128x128@2x.png
		COMMAND sips -Z 256 ${ICONSDIR}/256x256/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_256x256.png
		COMMAND sips -Z 512 ${ICONSDIR}/256x256/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_256x256@2x.png
		COMMAND sips -Z 512 ${ICONSDIR}/256x256/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_512x512.png
		COMMAND sips -Z 1024 ${ICONSDIR}/256x256/apps/hatari.png
		          --out ${CMAKE_BINARY_DIR}/Hatari.iconset/icon_512x512@2x.png
		COMMAND iconutil -c icns -o ${MACOSX_BUNDLE_ICON_FILE}
		          ${CMAKE_BINARY_DIR}/Hatari.iconset
	)
	# Create Hatari.app bundle
	add_custom_target(osx_bundle_dirs
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Frameworks
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/MacOS
			COMMAND cp ${MACOSX_BUNDLE_ICON_FILE}
				   ${BUNDLE_CONTENTS}/Resources/${MACOSX_BUNDLE_ICON_FILE}
			# Copy Localized .nib and help to Bundle
			COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/*.lproj ${BUNDLE_CONTENTS}/Resources/
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources/HatariHelp
			COMMAND cp -R ${GUIOSX_DOCS} ${BUNDLE_CONTENTS}/Resources/HatariHelp/
			#COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources/fr.lproj/HatariHelp
			#COMMAND cp -R ${GUIOSX_DOCS} ${BUNDLE_CONTENTS}/Resources/fr.lproj/HatariHelp/
			# Copy Frameworks to Bundle
			COMMAND cp -R ${SDL2_LIBDIR} ${BUNDLE_CONTENTS}/Frameworks/
			COMMAND ${COPY_PNG}
			DEPENDS ${MACOSX_BUNDLE_ICON_FILE}
	)
	add_dependencies(${APP_NAME} osx_bundle_dirs)
	set_source_files_properties(${GUIOSX_RSRCS} PROPERTIES
				    MACOSX_PACKAGE_LOCATION Resources)
	set_source_files_properties(${GUIOSX_DOCS} PROPERTIES
				    MACOSX_PACKAGE_LOCATION Resources/HatariHelp)

	#find ibtool
	find_program(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
	if (${IBTOOL} STREQUAL "IBTOOL-NOTFOUND")
		message(SEND_ERROR "ibtool can not be found and is needed to compile the .xib files.
			It should have been installed with the Apple developer tools. The default
			system paths were searched in addition to ${OSX_DEVELOPER_ROOT}/usr/bin")
	endif()
	# Compile the .xib files using the 'ibtool' program with the destination being the app package
	foreach(xib ${HATARI_XIBS})
		add_custom_command (TARGET ${APP_NAME} POST_BUILD
			COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text
				--compile ${BUNDLE_CONTENTS}/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${xib}.xib
			COMMAND rm ${BUNDLE_CONTENTS}/Resources/${xib}.xib
			COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${xib}.xib")
	endforeach()

# When building for Windows, add specific sources + method to compile .rc files
elseif(WIN32)
	# Set a default rc compiler if it was not defined yet
	if(NOT CMAKE_RC_COMPILER)
		set(CMAKE_RC_COMPILER windres)
	endif(NOT CMAKE_RC_COMPILER)
	ENABLE_LANGUAGE(RC)
	set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <FLAGS> -Ocoff -o <OBJECT> <SOURCE>")
	set_source_files_properties(${GUIWIN_RES} PROPERTIES LANGUAGE RC)
	add_executable(${APP_NAME} ${GUIWIN_RES} ${SOURCES} ${GUIWIN_SOURCES})

#emscripten needs some em-specific linker flags
elseif(EMSCRIPTEN)
	add_executable(${APP_NAME} ${SOURCES})
	set_target_properties(
		${APP_NAME}
		PROPERTIES
		LINK_FLAGS
		"-O3 \
-fno-exceptions \
-s USE_SDL=2 -s USE_ZLIB=1 -s INITIAL_MEMORY=262144000 -s ASYNCIFY -s -s ASSERTIONS=0 \
-s EXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\"]' \
-s EXPORTED_FUNCTIONS=\"['_main','_Reset_Warm','_Reset_Cold','_Statusbar_UpdateInfo','_Main_UnPauseEmulation','_Floppy_InsertDiskIntoDrive','_Floppy_SetDiskFileName','_Floppy_EjectDiskFromDrive','_Configuration_ChangeMemory','_Configuration_ChangeSystem','_Configuration_ChangeTos','_Configuration_ChangeUseHardDiskDirectories','_Configuration_ChangeFastForward','_Configuration_Apply','_IoMem_UnInit','_IoMem_Init']\" \
--preload-file ${CMAKE_BINARY_DIR}/files/@/share/hatari/ \
--pre-js ${CMAKE_SOURCE_DIR}/src/emscripten.js \
--shell-file ${CMAKE_SOURCE_DIR}/src/emscripten_shell.html"
	)
# Other targets, use default sources
else()
	add_executable(${APP_NAME} ${SOURCES})
endif(ENABLE_OSX_BUNDLE)

if(SDL2_OTHER_CFLAGS)
	target_compile_definitions(${APP_NAME} PRIVATE ${SDL2_OTHER_CFLAGS})
	# message(STATUS "Additional CFLAGS of SDL: ${SDL2_OTHER_CFLAGS}")
endif(SDL2_OTHER_CFLAGS)

target_link_libraries(${APP_NAME} Falcon UaeCpu GuiSdl Floppy Debug ${SDL2_LIBRARIES})

if(Math_FOUND AND NOT APPLE)
	target_link_libraries(${APP_NAME} ${MATH_LIBRARY})
endif()

if(Readline_FOUND)
	target_link_libraries(${APP_NAME} ${READLINE_LIBRARY})
endif(Readline_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(${APP_NAME} ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

if(PNG_FOUND)
	target_link_libraries(${APP_NAME} ${PNG_LIBRARY})
endif(PNG_FOUND)

if(X11_FOUND)
	target_link_libraries(${APP_NAME} ${X11_LIBRARIES})
endif(X11_FOUND)

if(PortMidi_FOUND)
	target_link_libraries(${APP_NAME} ${PORTMIDI_LIBRARY})
endif(PortMidi_FOUND)

if(CapsImage_FOUND)
	target_link_libraries(${APP_NAME} ${CAPSIMAGE_LIBRARY})
endif(CapsImage_FOUND)

if(Udev_FOUND)
	target_link_libraries(${APP_NAME} ${UDEV_LIBRARY})
endif(Udev_FOUND)

if(Capstone_FOUND)
	target_link_libraries(${APP_NAME} ${CAPSTONE_LIBRARY})
endif(Capstone_FOUND)

if(APPLE)
	target_link_libraries(${APP_NAME} "-framework Cocoa")
endif()

if(WIN32)
	# Needed for socket() on Windows
	target_link_libraries(${APP_NAME} ws2_32)
endif(WIN32)


if(ENABLE_OSX_BUNDLE)
	install(TARGETS ${APP_NAME} BUNDLE DESTINATION /Applications)
else()
	install(TARGETS ${APP_NAME} RUNTIME DESTINATION ${BINDIR})
	install(FILES hatari-icon.bmp DESTINATION ${DATADIR})
	file(GLOB TOS_IMG_FILE tos.img)
	if(TOS_IMG_FILE)
		install(FILES tos.img DESTINATION ${DATADIR})
	endif(TOS_IMG_FILE)
endif(ENABLE_OSX_BUNDLE)
