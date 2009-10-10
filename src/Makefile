# Main Makefile for Hatari.

# Use "export ENABLE_DSP_EMU=0" & "make clean"
# to disable experimental DSP emulation code.
ENABLE_DSP_EMU ?= 1

# Select CPU directory:
CPUDIR = uae-cpu

# Include settings
include ../Makefile.cnf

# Additional include directories:
CPPFLAGS += -I.. -I./includes -I$(CPUDIR) -I./debug -I./falcon \
 -DCONFDIR=\"$(CONFDIR)\" $(SDL_CFLAGS) $(PNG_CFLAGS) $(X11_CFLAGS)

ifeq ($(ENABLE_DSP_EMU),1)
CPPFLAGS += -DENABLE_DSP_EMU=1
endif

SRCS = audio.c avi_record.c bios.c blitter.c cart.c cfgopts.c \
  configuration.c options.c  createBlankImage.c change.c control.c \
  cycles.c dialog.c dim.c dmaSnd.c fdc.c file.c floppy.c \
  gemdos.c hd6301_cpu.c hdc.c ide.c ikbd.c int.c ioMem.c ioMemTabST.c \
  ioMemTabSTE.c ioMemTabTT.c ioMemTabFalcon.c joy.c keymap.c m68000.c \
  main.c midi.c memorySnapShot.c mfp.c msa.c paths.c  psg.c printer.c \
  rs232.c reset.c rtc.c scandir.c st.c stMemory.c screen.c screenSnapShot.c \
  shortcut.c sound.c spec512.c statusbar.c str.c tos.c unzip.c utils.c \
  vdi.c video.c wavFormat.c xbios.c ymFormat.c zip.c

OBJS = $(SRCS:.c=.o)

SUBDIRLIBS = $(CPUDIR)/cpu68k.a debug/debug.a falcon/falcon.a gui-sdl/gui-sdl.a

ifeq ($(SYS_WINDOWS),1)
  SUBDIRLIBS += gui-win/gui-win.a 
  OBJS += gui-win/hatari-winicon.o
else
ifneq ($(strip $(shell gcc -v 2>&1 |grep "cygwin")),)
  SUBDIRLIBS += gui-win/gui-win.a 
endif 
endif

SUBDIRS = $(dir $(SUBDIRLIBS))
ALLOBJS = $(OBJS) $(SUBDIRLIBS)


.PHONY : all
all: ../Makefile.cnf
	$(MAKE) $(SUBDIRS)
	$(MAKE) hatari

LIBS := $(PORTAUDIO_LIBS) $(PNG_LIBS) $(SDL_LIBS) $(X11_LIBS) $(LIBS)

hatari: $(ALLOBJS)
	$(CC) $(LDFLAGS) $(ALLOBJS) $(LIBS) -o hatari

# Sub-folder dependencies:
.PHONY : $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) ENABLE_DSP_EMU=$(ENABLE_DSP_EMU) CPUDIR=$(CPUDIR) -C $@


clean:
	$(RM) *.o hatari
	$(MAKE) -C gui-sdl/ clean
	$(MAKE) -C gui-win/ clean
	$(MAKE) -C $(CPUDIR) clean
	$(MAKE) -C falcon/ clean
	$(MAKE) -C debug/ clean

distclean:
	$(RM) *.o hatari
	$(RM) Makefile.dep *~ *.bak *.orig
	$(RM) convert/*~ convert/*.bak
	$(MAKE) -C gui-sdl/ distclean
	$(MAKE) -C gui-win/ distclean
	$(MAKE) -C $(CPUDIR) distclean
	$(MAKE) -C falcon/ distclean
	$(MAKE) -C debug/ distclean


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
	$(MAKE) -C debug/ depend

.PHONY : cleandeps
cleandeps:
	$(RM) Makefile.dep */Makefile.dep

-include Makefile.dep
