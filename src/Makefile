# Main Makefile for Hatari.

# Use "export ENABLE_DSP_EMU=0" & "make clean"
# to disable experimental DSP emulation code.
ENABLE_DSP_EMU ?= 1

# Select CPU directory:
CPUDIR = uae-cpu

# Include settings
include ../Makefile.cnf

# Additional include directories:
CPPFLAGS += -I.. -I./includes -I$(CPUDIR) -I./falcon
CPPFLAGS += -DCONFDIR=\"$(CONFDIR)\" $(SDL_CFLAGS)

ifeq ($(ENABLE_DSP_EMU),1)
CPPFLAGS += -DENABLE_DSP_EMU=1
endif

SRCS = audio.c bios.c blitter.c cart.c cfgopts.c configuration.c options.c \
  createBlankImage.c change.c control.c cycles.c debugui.c dialog.c dim.c \
  dmaSnd.c fdc.c file.c floppy.c gemdos.c hdc.c ide.c ikbd.c int.c ioMem.c \
  ioMemTabST.c ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c keymap.c \
  log.c m68000.c main.c midi.c memorySnapShot.c mfp.c msa.c paths.c \
  psg.c printer.c rs232.c reset.c rtc.c scandir.c st.c stMemory.c screen.c \
  screenSnapShot.c shortcut.c sound.c spec512.c statusbar.c str.c tos.c \
  unzip.c utils.c vdi.c video.c wavFormat.c xbios.c ymFormat.c zip.c

OBJS = $(SRCS:.c=.o)

SUBDIRLIBS = $(CPUDIR)/cpu68k.a falcon/falcon.a gui-sdl/gui-sdl.a

ifeq ($(SYS_WINDOWS),1)
  ALLOBJS += gui-win/gui-win.a
else
ifneq ($(strip $(shell gcc -v 2>&1 |grep "cygwin")),)
  ALLOBJS += gui-win/gui-win.a
endif 
endif

ALLOBJS = $(OBJS) $(SUBDIRLIBS)


all: ../Makefile.cnf hatari

hatari: $(ALLOBJS)
	$(CC) $(LDFLAGS) $(ALLOBJS) $(SDL_LIBS) $(LIBS) -o hatari

# Sub-folder dependencies:
.PHONY : $(SUBDIRLIBS)
$(SUBDIRLIBS):
	$(MAKE) ENABLE_DSP_EMU=$(ENABLE_DSP_EMU) CPUDIR=$(CPUDIR) -C $(dir $@)


clean:
	$(RM) *.o hatari
	$(MAKE) -C gui-sdl/ clean
	$(MAKE) -C gui-win/ clean
	$(MAKE) -C $(CPUDIR) clean
	$(MAKE) -C falcon/ clean

distclean:
	$(RM) *.o hatari
	$(RM) Makefile.dep *~ *.bak *.orig
	$(RM) convert/*~ convert/*.bak
	$(MAKE) -C gui-sdl/ distclean
	$(MAKE) -C gui-win/ distclean
	$(MAKE) -C $(CPUDIR) distclean
	$(MAKE) -C falcon/ distclean


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile ../Makefile.cnf
	$(CC) -M $(CPPFLAGS) $(SRCS) > Makefile.dep

.PHONY : depend
depend:
	$(MAKE) cleandeps
	$(MAKE) Makefile.dep
	$(MAKE) -C gui-sdl/ depend
	$(MAKE) -C $(CPUDIR) depend
	$(MAKE) -C falcon/ depend

.PHONY : cleandeps
cleandeps:
	$(RM) Makefile.dep */Makefile.dep

-include Makefile.dep
