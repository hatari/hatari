# Makefile for Hatari's SDL GUI.

# Include settings
include ../../Makefile.cnf

# Additional include directories:
INCFLAGS = -I../includes $(CPPFLAGS)


CFLAGS += -DDATADIR=\"$(DATADIR)\" $(INCFLAGS) $(SDL_CFLAGS) -Wno-write-strings



DLGSRCS = dlgAbout.c dlgAlert.c dlgDevice.c dlgDisc.c dlgJoystick.c \
  dlgKeyboard.c dlgMain.c dlgMemory.c dlgNewDisc.c dlgRom.c \
  dlgScreen.c dlgSound.c dlgSystem.c dlgFileSelect.c sdlgui.c


DLGOBJS = $(DLGSRCS:.c=.o)


all: $(DLGOBJS)

font5x8.h: font5x8.bmp
	convert font5x8.bmp xbm:font5x8.h

font10x16.h: font10x16.bmp
	convert font10x16.bmp xbm:font10x16.h


clean:
	$(RM) *.o

distclean: clean
	$(RM) Makefile.dep *~ *.bak *.orig


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile
	$(CC) -MM $(CFLAGS) $(DLGSRCS) > Makefile.dep

depend: Makefile.dep

-include Makefile.dep
