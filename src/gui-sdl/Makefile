# Makefile for Hatari's SDL GUI.

# Include settings
include ../../Makefile.cnf

# Additional include directories:
INCFLAGS = -I../includes


CFLAGS += -DDATADIR=\"$(DATADIR)\" $(INCFLAGS) $(SDL_CFLAGS)



DLGSRCS = dlgAbout.c dlgAlert.c dlgDevice.c dlgDisc.c dlgJoystick.c \
  dlgKeyboard.c dlgMain.c dlgMemory.c dlgNewDisc.c dlgRom.c \
  dlgScreen.c dlgSound.c dlgSystem.c dlgFileSelect.c sdlgui.c


DLGOBJS = $(DLGSRCS:.c=.o)


all: $(DLGOBJS)

clean:
	rm -f *.o

font5x8.h: font5x8.bmp
	convert font5x8.bmp xbm:font5x8.h

font10x16.h: font10x16.bmp
	convert font10x16.bmp xbm:font10x16.h

# Use "make depend" to generate file dependencies:
depend: $(DLGSRCS)
	$(CC) -MM $(CFLAGS) $(DLGSRCS) > Makefile.dep

ifneq (,$(wildcard Makefile.dep))
include Makefile.dep
endif
