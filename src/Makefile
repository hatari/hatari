# Main Makefile for Hatari.

# Include settings
include ../Makefile.cnf

# Additional include directories:
INCFLAGS = -I./includes -I./uae-cpu


CFLAGS += -DDATADIR=\"$(DATADIR)\" -DCONFDIR=\"$(CONFDIR)\" $(INCFLAGS) $(SDL_CFLAGS)


CPUOBJS = ./uae-cpu/hatari-glue.o ./uae-cpu/memory.o ./uae-cpu/newcpu.o \
  ./uae-cpu/readcpu.o ./uae-cpu/fpp.o ./uae-cpu/cpustbl.o ./uae-cpu/cpudefs.o \
  ./uae-cpu/cpuemu1.o ./uae-cpu/cpuemu2.o ./uae-cpu/cpuemu3.o \
  ./uae-cpu/cpuemu4.o ./uae-cpu/cpuemu5.o ./uae-cpu/cpuemu6.o \
  ./uae-cpu/cpuemu7.o ./uae-cpu/cpuemu8.o

GUIOBJS = ./gui-sdl/dlgAbout.o ./gui-sdl/dlgAlert.o ./gui-sdl/dlgDevice.o \
  ./gui-sdl/dlgDisc.o ./gui-sdl/dlgJoystick.o ./gui-sdl/dlgKeyboard.o \
  ./gui-sdl/dlgMain.o ./gui-sdl/dlgMemory.o ./gui-sdl/dlgNewDisc.o \
  ./gui-sdl/dlgRom.o ./gui-sdl/dlgScreen.o ./gui-sdl/dlgSound.o  \
  ./gui-sdl/dlgSystem.o ./gui-sdl/dlgFileSelect.o ./gui-sdl/sdlgui.o


SRCS = audio.c bios.c blitter.c cart.c cfgopts.c configuration.c options.c \
  createBlankImage.c cycles.c debugui.c dialog.c dim.c dmaSnd.c fdc.c file.c \
  floppy.c gemdos.c hdc.c ikbd.c int.c ioMem.c ioMemTabST.c ioMemTabSTE.c \
  ioMemTabTT.c ioMemTabFalcon.c joy.c keymap.c \
  log.c m68000.c main.c midi.c memorySnapShot.c mfp.c misc.c msa.c psg.c \
  printer.c rs232.c reset.c rtc.c scandir.c st.c stMemory.c screen.c \
  screenSnapShot.c shortcut.c sound.c spec512.c tos.c unzip.c vdi.c video.c \
  wavFormat.c xbios.c ymFormat.c zip.c

OBJS = $(SRCS:.c=.o)


ALLOBJS = $(OBJS) $(GUIOBJS) $(CPUOBJS)


all: hatari

hatari: $(ALLOBJS)
	$(CC) $(LDFLAGS) $(ALLOBJS) $(SDL_LIBS) $(LIBS) -o hatari

# Sub-folder dependencies:
uae-cpu/%.o: uae-cpu/%.c
	$(MAKE) -C uae-cpu/ all
uae-cpu/cpuemu%.o: uae-cpu/cpuemu.c
	$(MAKE) -C uae-cpu/ all
uae-cpu/cpuemu.c: uae-cpu/gencpu.o uae-cpu/readcpu.c uae-cpu/cpudefs.c
	$(MAKE) -C uae-cpu/ cpuemu.c
uae-cpu/cpudefs.c: uae-cpu/build68k.c uae-cpu/table68k
	$(MAKE) -C uae-cpu/ cpudefs.c

gui-sdl/%.o: gui-sdl/%.c
	$(MAKE) -C gui-sdl/ all


clean:
	$(RM) *.o hatari
	$(MAKE) -C gui-sdl/ clean
	$(MAKE) -C uae-cpu/ clean

distclean:
	$(RM) *.o hatari
	$(RM) Makefile.dep *~ *.bak *.orig
	$(MAKE) -C gui-sdl/ distclean
	$(MAKE) -C uae-cpu/ distclean


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile
	$(CC) -MM $(CFLAGS) $(SRCS) > Makefile.dep

depend: Makefile.dep
	$(MAKE) -C gui-sdl/ depend

-include Makefile.dep
