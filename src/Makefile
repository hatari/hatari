# Makefile for Hatari

# Set flags passed to the compiler (e.g. optimization flags)
CMPLRFLAGS = -g -O2

# Set the C compiler (e.g. gcc)
CC = gcc

# Here you can define the default data directory for Hatari where it
# looks for the default TOS image etc.
# For example you can use the local directory with . or if you want to do
# a system-wide installation, use something like /usr/share/hatari
DATADIR = .

# SDL-Library configuration:
SDLLIB=$(shell sdl-config --libs)
SDLFLAGS=$(shell sdl-config --cflags)


CFLAGS = -I./includes -I./uae-cpu $(CMPLRFLAGS) $(SDLFLAGS)


CPUOBJS = ./uae-cpu/hatari-glue.o ./uae-cpu/memory.o ./uae-cpu/newcpu.o \
  ./uae-cpu/readcpu.o ./uae-cpu/fpp.o ./uae-cpu/cpustbl.o ./uae-cpu/cpudefs.o \
  ./uae-cpu/cpuemu1.o ./uae-cpu/cpuemu2.o ./uae-cpu/cpuemu3.o \
  ./uae-cpu/cpuemu4.o ./uae-cpu/cpuemu5.o ./uae-cpu/cpuemu6.o \
  ./uae-cpu/cpuemu7.o ./uae-cpu/cpuemu8.o

SRCS = audio.c bios.c cart.c configuration.c createBlankImage.c \
  createDiscImage.c debug.c debugui.c dialog.c errlog.c fdc.c file.c \
  floppy.c gemdos.c hdc.c ikbd.c int.c intercept.c joy.c keymap.c m68000.c \
  main.c midi.c memAlloc.c memorySnapShot.c mfp.c misc.c msa.c psg.c \
  printer.c rs232.c reset.c st.c stMemory.c screen.c screenConvert.c \
  screenSnapShot.c sdlgui.c shortcut.c sound.c spec512.c timer.c \
  tos.c vdi.c video.c wavFormat.c xbios.c ymFormat.c

OBJS = $(SRCS:.c=.o)


hatari: $(OBJS) cpu
	$(CC) $(OBJS) $(CPUOBJS) $(SDLLIB) -o hatari

cpu:
	$(MAKE) -C uae-cpu/ CFLAGS="$(CMPLRFLAGS) $(SDLFLAGS)" CC="$(CC)" all

clean:
	rm -f *.o hatari
	$(MAKE) -C uae-cpu/ clean


configuration.o: configuration.c
	$(CC) $(CFLAGS) -c -DDATADIR=\"$(DATADIR)\" configuration.c -o configuration.o

sdlgui.o: sdlgui.c
	$(CC) $(CFLAGS) -c -DDATADIR=\"$(DATADIR)\" sdlgui.c -o sdlgui.o

screenConvert.o: convert/high640x1.c convert/low640x16.c convert/spec320x16.c \
    convert/vdi4.c convert/high640x8.c convert/low640x8.c convert/spec640x16.c \
    convert/low320x16.c convert/med640x16.c convert/vdi16.c \
    convert/low320x8.c  convert/med640x8.c  convert/vdi2.c
