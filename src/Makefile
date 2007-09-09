# Main Makefile for Hatari.

# Set ENABLE_DSP_EMU to 1 to enable experimental DSP emulation code
# (there are currently no known programs which this would help,
# but it will make many of the Falcon programs not to work).
# If you change this, do "make clean"
ENABLE_DSP_EMU = 0

# Select CPU directory:
CPUDIR = uae-cpu

# Include settings
include ../Makefile.cnf

# Additional include directories:
CPPFLAGS += -I.. -I./includes -I$(CPUDIR) -I./falcon
CPPFLAGS += -DDATADIR=\"$(DATADIR)\" -DCONFDIR=\"$(CONFDIR)\" $(SDL_CFLAGS)

ifeq ($(ENABLE_DSP_EMU),1)
CPPFLAGS += -DENABLE_DSP_EMU=1
endif

GUIOBJS = ./gui-sdl/dlgAbout.o ./gui-sdl/dlgAlert.o ./gui-sdl/dlgDevice.o \
  ./gui-sdl/dlgDisc.o ./gui-sdl/dlgJoystick.o ./gui-sdl/dlgKeyboard.o \
  ./gui-sdl/dlgMain.o ./gui-sdl/dlgMemory.o ./gui-sdl/dlgNewDisc.o \
  ./gui-sdl/dlgRom.o ./gui-sdl/dlgScreen.o ./gui-sdl/dlgSound.o  \
  ./gui-sdl/dlgSystem.o ./gui-sdl/dlgFileSelect.o ./gui-sdl/sdlgui.o

GUIWINOBJS = ./gui-win/hatari-winicon.o ./gui-win/opencon.o

FALCOBJS = ./falcon/videl.o ./falcon/hostscreen.o ./falcon/nvram.o
ifeq ($(ENABLE_DSP_EMU),1)
FALCOBJS += ./falcon/dsp.o ./falcon/dsp_cpu.o ./falcon/dsp_disasm.o
endif

SRCS = audio.c bios.c blitter.c cart.c cfgopts.c configuration.c options.c \
  createBlankImage.c cycles.c debugui.c dialog.c dim.c dmaSnd.c fdc.c file.c \
  floppy.c gemdos.c hdc.c ide.c ikbd.c int.c ioMem.c ioMemTabST.c \
  ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c keymap.c \
  log.c m68000.c main.c midi.c memorySnapShot.c mfp.c misc.c msa.c psg.c \
  printer.c rs232.c reset.c rtc.c scandir.c st.c stMemory.c screen.c \
  screenSnapShot.c shortcut.c sound.c spec512.c tos.c unzip.c vdi.c video.c \
  wavFormat.c xbios.c ymFormat.c zip.c

OBJS = $(SRCS:.c=.o)


ALLOBJS = $(OBJS) $(GUIOBJS) $(CPUDIR)/cpu68k.a $(FALCOBJS)

ifeq ($(SYS_WINDOWS),1)
  ALLOBJS += $(GUIWINOBJS)
endif
ifneq ($(strip $(shell gcc -v 2>&1 |grep "cygwin")),)
  ALLOBJS += $(GUIWINOBJS)
endif 


all: hatari

hatari: $(ALLOBJS)
	$(CC) $(LDFLAGS) $(ALLOBJS) $(SDL_LIBS) $(LIBS) -o hatari

# Sub-folder dependencies:
.PHONY : $(CPUDIR)/cpu68k.a
$(CPUDIR)/cpu68k.a:
	$(MAKE) -C $(CPUDIR)

gui-sdl/%.o: gui-sdl/%.c
	$(MAKE) ENABLE_DSP_EMU=$(ENABLE_DSP_EMU) -C gui-sdl/ all

gui-win/%.o:
	$(MAKE) ENABLE_DSP_EMU=$(ENABLE_DSP_EMU) -C gui-win/ all

falcon/%.o: falcon/%.c
	$(MAKE) ENABLE_DSP_EMU=$(ENABLE_DSP_EMU) CPUDIR=$(CPUDIR) -C falcon/


clean:
	$(RM) *.o hatari
	$(MAKE) -C gui-sdl/ clean
	$(MAKE) -C gui-win/ clean
	$(MAKE) -C $(CPUDIR) clean
	$(MAKE) -C falcon/ clean

distclean:
	$(RM) *.o hatari
	$(RM) Makefile.dep *~ *.bak *.orig
	$(MAKE) -C gui-sdl/ distclean
	$(MAKE) -C gui-win/ distclean
	$(MAKE) -C $(CPUDIR) distclean
	$(MAKE) -C falcon/ distclean


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile
	$(CC) -M $(CPPFLAGS) $(SRCS) > Makefile.dep

depend: Makefile.dep
	$(MAKE) -C gui-sdl/ depend

-include Makefile.dep
