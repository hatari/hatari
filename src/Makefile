# Makefile for Hatari

# Set the C compiler (e.g. gcc)
CC = gcc

# Set flags passed to the compiler (e.g. optimization flags)
CMPLRFLAGS = -g -O2 -Wall -Wno-unused

# Here you can define the default data directory for Hatari where it
# looks for the default TOS image etc.
# For example you can use the local directory with . or if you want to do
# a system-wide installation, use something like /usr/share/hatari
DATADIR = .

# SDL-Library configuration - you don't have to change this if you have
# correctly installed the SDL library and if you are using GNU make!
SDLLIB = $(shell sdl-config --libs)
SDLFLAGS = $(shell sdl-config --cflags)

# Additional include directories:
INCFLAGS = -I./includes -I./uae-cpu

# Additional libaries:
LIBFLAGS = -lz

CFLAGS = $(CMPLRFLAGS) $(INCFLAGS) $(SDLFLAGS)


CPUOBJS = ./uae-cpu/hatari-glue.o ./uae-cpu/memory.o ./uae-cpu/newcpu.o \
  ./uae-cpu/readcpu.o ./uae-cpu/fpp.o ./uae-cpu/cpustbl.o ./uae-cpu/cpudefs.o \
  ./uae-cpu/cpuemu1.o ./uae-cpu/cpuemu2.o ./uae-cpu/cpuemu3.o \
  ./uae-cpu/cpuemu4.o ./uae-cpu/cpuemu5.o ./uae-cpu/cpuemu6.o \
  ./uae-cpu/cpuemu7.o ./uae-cpu/cpuemu8.o

SRCS = audio.c bios.c blitter.c cart.c cfgopts.c configuration.c \
  createBlankImage.c createDiscImage.c debug.c debugui.c dialog.c errlog.c \
  fdc.c file.c floppy.c gemdos.c hdc.c ikbd.c int.c intercept.c joy.c keymap.c \
  m68000.c main.c midi.c memAlloc.c memorySnapShot.c mfp.c misc.c msa.c psg.c \
  printer.c rs232.c reset.c rtc.c st.c stMemory.c screen.c screenConvert.c \
  screenSnapShot.c sdlgui.c shortcut.c sound.c spec512.c \
  tos.c unzip.c vdi.c video.c wavFormat.c xbios.c ymFormat.c zip.c

OBJS = $(SRCS:.c=.o)


hatari: $(OBJS) cpu
	$(CC) $(OBJS) $(CPUOBJS) $(SDLLIB) $(LIBFLAGS) -o hatari
#	strip hatari

cpu:
	$(MAKE) -C uae-cpu/ CC="$(CC)" CMPLRFLAGS="$(CMPLRFLAGS)" SDLFLAGS="$(SDLFLAGS)" all

clean:
	rm -f *.o hatari
	$(MAKE) -C uae-cpu/ clean


configuration.o: configuration.c
	$(CC) $(CFLAGS) -c -DDATADIR=\"$(DATADIR)\" configuration.c -o configuration.o

sdlgui.o: sdlgui.c
	$(CC) $(CFLAGS) -c -DDATADIR=\"$(DATADIR)\" sdlgui.c -o sdlgui.o


# Use "make depend" to generate file dependencies:
depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $(SRCS) > Makefile.dep

ifneq (,$(wildcard Makefile.dep))
include Makefile.dep
endif

