
include_directories(.. includes debug falcon uae-cpu ${SDL_INCLUDE_DIR}) 

link_directories(debug falcon gui-sdl uae-cpu) 

add_executable(hatari
	audio.c avi_record.c bios.c blitter.c cart.c cfgopts.c
	configuration.c options.c  createBlankImage.c change.c
	control.c cycles.c dialog.c dim.c dmaSnd.c fdc.c file.c
	floppy.c gemdos.c hd6301_cpu.c hdc.c ide.c ikbd.c int.c ioMem.c
	ioMemTabST.c ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c
	keymap.c m68000.c main.c midi.c memorySnapShot.c mfp.c msa.c
	paths.c  psg.c printer.c rs232.c reset.c rtc.c scandir.c st.c
	stMemory.c screen.c screenSnapShot.c shortcut.c sound.c
	spec512.c statusbar.c str.c tos.c unzip.c utils.c vdi.c
	video.c wavFormat.c xbios.c ymFormat.c zip.c
	)

target_link_libraries(hatari
	Debug Falcon GuiSdl UaeCpu
	${SDL_LIBRARY}
	SDLmain # Sadly not included in SDL_LIBRARY variable
	)

add_subdirectory(debug)
add_subdirectory(falcon)
add_subdirectory(gui-sdl)
add_subdirectory(uae-cpu)
