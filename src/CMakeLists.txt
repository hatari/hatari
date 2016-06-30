
set(SOURCES
	acia.c audio.c avi_record.c bios.c blitter.c cart.c cfgopts.c
	clocks_timings.c configuration.c options.c change.c control.c
	cycInt.c cycles.c dialog.c dmaSnd.c fdc.c file.c floppy.c
	floppy_ipf.c floppy_stx.c gemdos.c hd6301_cpu.c hdc.c ide.c ikbd.c
	ioMem.c ioMemTabST.c ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c
	keymap.c m68000.c main.c midi.c memorySnapShot.c mfp.c nf_scsidrv.c
	paths.c  psg.c printer.c resolution.c rs232.c reset.c rtc.c
	scandir.c stMemory.c screen.c screenConvert.c screenSnapShot.c
	shortcut.c sound.c spec512.c statusbar.c str.c tos.c utils.c
	vdi.c video.c wavFormat.c xbios.c ymFormat.c)

# Disk image code is shared with the hmsa tool, so we put it into a library:
add_library(Floppy createBlankImage.c dim.c msa.c st.c zip.c)

# When building for OSX, define specific sources for gui and ressources
if(ENABLE_OSX_BUNDLE)
	set(GUIOSX_SOURCES
	    gui-osx/AlertHooks.m gui-osx/PrefsController.m gui-osx/Shared.m
	    gui-osx/CreateFloppyController.m gui-osx/SDLMain.m)
	set_source_files_properties(${GUIOSX_SOURCES} PROPERTIES LANGUAGE C)
	set(GUIOSX_RSRCS
	    gui-osx/Hatari.icns gui-osx/en.lproj gui-osx/fr.lproj)
	set(GUIOSX_DOCS
	    ${CMAKE_SOURCE_DIR}/doc/manual.html ${CMAKE_SOURCE_DIR}/doc/images
	    ${CMAKE_SOURCE_DIR}/doc/compatibility.html)
	# these are the OS X Interface Builder Files
	set (HATARI_XIBS en.lproj/SDLMain fr.lproj/SDLMain)
endif(ENABLE_OSX_BUNDLE)

# When building for Windows, define specific sources for gui and resources
# and set the subsystem of the resulting .exe to "windows GUI" instead of "console"
if(WIN32)
	set(GUIWIN_SOURCES gui-win/opencon.c)
	set(GUIWIN_RES gui-win/hatari-winicon.rc)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
endif(WIN32)

if(ENABLE_WINUAE_CPU)
	set(CPUDIR cpu)
else()
	set(CPUDIR uae-cpu)
endif(ENABLE_WINUAE_CPU)

include_directories(${CMAKE_BINARY_DIR} ${SDL_INCLUDE_DIR} ${CPUDIR}
		    includes debug falcon)

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
	set(SOURCES ${SOURCES} unzip.c)
endif(ZLIB_FOUND)

if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIR})
endif(PNG_FOUND)

if(X11_FOUND)
	include_directories(${X11_INCLUDE_DIR})
endif(X11_FOUND)


link_directories(${CMAKE_CURRENT_BINARY_DIR}/debug
		 ${CMAKE_CURRENT_BINARY_DIR}/falcon
		 ${CMAKE_CURRENT_BINARY_DIR}/gui-sdl
		 ${CMAKE_CURRENT_BINARY_DIR}/${CPUDIR})

add_subdirectory(debug)
add_subdirectory(falcon)
add_subdirectory(gui-sdl)
add_subdirectory(${CPUDIR})

# When building for OSX, add specific sources
if(ENABLE_OSX_BUNDLE)
	#find_library(SDL_FRAMEWORK SDL ${ADD_FRAMEWOK_PATH})
	set(ADDITIONAL_INCLUDES ${FRAMEWORKS})
	set_source_files_properties(${FRAMEWORKS} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks)
	add_executable(hatari MACOSX_BUNDLE ${GUIOSX_RSRCS} ${GUIOSX_DOCS} ${SOURCES} ${GUIOSX_SOURCES} ${FRAMEWORKS})
	set_target_properties(hatari PROPERTIES MACOSX_BUNDLE_INFO_PLIST
			      ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/Info-Hatari.plist)
	set(MACOSX_BUNDLE_ICON_FILE Hatari.icns)
	if(CMAKE_GENERATOR MATCHES "Xcode")
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/\${CONFIGURATION}/Hatari.app/Contents)
	else()
		set(BUNDLE_CONTENTS ${CMAKE_CURRENT_BINARY_DIR}/Hatari.app/Contents)
	endif()
	# Create Hatari.app bundle
	add_custom_target(osx_bundle_dirs
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources
			#COMMAND mkdir -p ${BUNDLE_CONTENTS}/Frameworks
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/MacOS
			COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${MACOSX_BUNDLE_ICON_FILE}
				   ${BUNDLE_CONTENTS}/Resources/${MACOSX_BUNDLE_ICON_FILE}
			# Copy Localized .nib to Bundle
			COMMAND cp -R ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/*.lproj ${BUNDLE_CONTENTS}/Resources/
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources/en.lproj/HatariHelp
			COMMAND cp -R ${GUIOSX_DOCS} ${BUNDLE_CONTENTS}/Resources/en.lproj/HatariHelp/
			COMMAND mkdir -p ${BUNDLE_CONTENTS}/Resources/fr.lproj/HatariHelp
			COMMAND cp -R ${GUIOSX_DOCS} ${BUNDLE_CONTENTS}/Resources/fr.lproj/HatariHelp/
	)
	add_dependencies(hatari osx_bundle_dirs)
	set_source_files_properties(${GUIOSX_RSRCS} PROPERTIES
				    MACOSX_PACKAGE_LOCATION Resources)
	set_source_files_properties(${GUIOSX_DOCS} PROPERTIES
				    MACOSX_PACKAGE_LOCATION Resources/en.lproj/HatariHelp)

	#find ibtool
	find_program(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
	if (${IBTOOL} STREQUAL "IBTOOL-NOTFOUND")
		message(SEND_ERROR "ibtool can not be found and is needed to compile the .xib files.
			It should have been installed with the Apple developer tools. The default
			system paths were searched in addition to ${OSX_DEVELOPER_ROOT}/usr/bin")
	endif()
	# Compile the .xib files using the 'ibtool' program with the destination being the app package
	foreach(xib ${HATARI_XIBS})
		add_custom_command (TARGET hatari POST_BUILD
			COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text
				--compile ${BUNDLE_CONTENTS}/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${xib}.xib
			COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/gui-osx/${xib}.xib")
	endforeach()

# When building for Windows, add specific sources + method to compile .rc files
elseif(WIN32)
	# Set a default rc compiler if it was not defined yet
	if(NOT CMAKE_RC_COMPILER)
		set(CMAKE_RC_COMPILER windres)
	endif(NOT CMAKE_RC_COMPILER)
	ENABLE_LANGUAGE(RC)
	set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -Ocoff -o <OBJECT> <SOURCE>")
	set_source_files_properties(${GUIWIN_RES} PROPERTIES LANGUAGE RC)
	add_executable(hatari ${GUIWIN_RES} ${SOURCES} ${GUIWIN_SOURCES})

# Other targets, use default sources
else()
	add_executable(hatari ${SOURCES})
endif(ENABLE_OSX_BUNDLE)

target_link_libraries(hatari Falcon UaeCpu GuiSdl Floppy Debug ${SDL_LIBRARY})

if(MATH_FOUND AND NOT APPLE)
	target_link_libraries(hatari ${MATH_LIBRARY})
endif()

if(SDLMAIN_LIBRARY)
	target_link_libraries(hatari ${SDLMAIN_LIBRARY})
endif(SDLMAIN_LIBRARY)

if(READLINE_FOUND)
	target_link_libraries(hatari ${READLINE_LIBRARY})
endif(READLINE_FOUND)

if(ZLIB_FOUND)
	target_link_libraries(hatari ${ZLIB_LIBRARY})
endif(ZLIB_FOUND)

if(PNG_FOUND)
	target_link_libraries(hatari ${PNG_LIBRARY})
endif(PNG_FOUND)

if(X11_FOUND)
	target_link_libraries(hatari ${X11_LIBRARIES})
endif(X11_FOUND)

if(PORTAUDIO_FOUND)
	target_link_libraries(hatari ${PORTAUDIO_LIBRARY})
endif(PORTAUDIO_FOUND)

if(CAPSIMAGE_FOUND)
	target_link_libraries(hatari ${CAPSIMAGE_LIBRARY})
endif(CAPSIMAGE_FOUND)

if(UDEV_FOUND)
	target_link_libraries(hatari ${UDEV_LIBRARY})
endif(UDEV_FOUND)

if(WIN32)
	# Needed for socket() on Windows
	target_link_libraries(hatari ws2_32)
endif(WIN32)


if(ENABLE_OSX_BUNDLE)
	install(TARGETS hatari BUNDLE DESTINATION /Applications)
else()
	install(TARGETS hatari RUNTIME DESTINATION ${BINDIR})
	install(FILES hatari-icon.bmp DESTINATION ${DATADIR})
	file(GLOB TOS_IMG_FILE tos.img)
	if(TOS_IMG_FILE)
		install(FILES tos.img DESTINATION ${DATADIR})
	endif(TOS_IMG_FILE)
endif(ENABLE_OSX_BUNDLE)
