
set(SOURCES
	audio.c avi_record.c bios.c blitter.c cart.c cfgopts.c
	configuration.c options.c  createBlankImage.c change.c
	control.c cycInt.c cycles.c dialog.c dim.c dmaSnd.c fdc.c file.c
	floppy.c gemdos.c hd6301_cpu.c hdc.c ide.c ikbd.c ioMem.c
	ioMemTabST.c ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c
	keymap.c m68000.c main.c midi.c memorySnapShot.c mfp.c msa.c
	paths.c  psg.c printer.c rs232.c reset.c rtc.c scandir.c st.c
	stMemory.c screen.c screenSnapShot.c shortcut.c sound.c
	spec512.c statusbar.c str.c tos.c unzip.c utils.c vdi.c
	video.c wavFormat.c xbios.c ymFormat.c zip.c)

if(ENABLE_OSX_BUNDLE)
	set(GUIOSX_SOURCES
	    gui-osx/AlertHooks.m gui-osx/PrefsController.m gui-osx/Shared.m
	    gui-osx/CreateFloppyController.m gui-osx/SDLMain.m)
	set_source_files_properties(${GUIOSX_SOURCES} PROPERTIES LANGUAGE C)
	set(GUIOSX_RSRCS
	    gui-osx/SDLMain.nib gui-osx/Hatari.icns gui-osx/English.lproj)
endif(ENABLE_OSX_BUNDLE)

include_directories(${CMAKE_BINARY_DIR} includes debug falcon uae-cpu
		    ${SDL_INCLUDE_DIR})

if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
endif(ZLIB_FOUND)

if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIR})
endif(PNG_FOUND)

if(X11_FOUND)
	include_directories(${X11_INCLUDE_DIR})
endif(X11_FOUND)


link_directories(debug falcon gui-sdl uae-cpu) 

add_subdirectory(debug)
add_subdirectory(falcon)
add_subdirectory(gui-sdl)
add_subdirectory(uae-cpu)

if(ENABLE_OSX_BUNDLE)
	set_source_files_properties(${GUIOSX_RSRCS}
			PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	add_executable(hatari MACOSX_BUNDLE ${GUIOSX_RSRCS} ${SOURCES} ${GUIOSX_SOURCES})
	# set_target_properties(hatari PROPERTIES MACOSX_BUNDLE true)
	set_target_properties(hatari PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../Info-Hatari.plist)
else()
	add_executable(hatari ${SOURCES})
endif(ENABLE_OSX_BUNDLE)

target_link_libraries(hatari Debug Falcon GuiSdl UaeCpu ${SDL_LIBRARY})

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
	target_link_libraries(hatari ${X11_X11_LIB})
endif(X11_FOUND)

if(PORTAUDIO_FOUND)
	target_link_libraries(hatari ${PORTAUDIO_LIBRARY})
endif(PORTAUDIO_FOUND)

if(ENABLE_OSX_BUNDLE)
	install(TARGETS hatari BUNDLE DESTINATION /Applications)
else()
	install(TARGETS hatari RUNTIME DESTINATION ${BINDIR})
endif(ENABLE_OSX_BUNDLE)
