# Makefile for Hatari's SDL GUI.

# Include settings
include ../../Makefile.cnf

# Additional include directories:
INCFLAGS = -I../includes


CFLAGS += -DDATADIR=\"$(DATADIR)\" $(INCFLAGS) $(SDL_CFLAGS)



DLGSRCS = dlgAbout.c dlgDevice.c dlgDisc.c dlgJoystick.c dlgKeyboard.c \
  dlgMain.c dlgMemory.c dlgScreen.c dlgSound.c dlgSystem.c dlgTosGem.c \
  dlgFileSelect.c sdlgui.c


DLGOBJS = $(DLGSRCS:.c=.o)


all: $(DLGOBJS)

clean:
	rm -f *.o

# Use "make depend" to generate file dependencies:
depend: $(DLGSRCS)
	$(CC) -MM $(CFLAGS) $(DLGSRCS) > Makefile.dep

ifneq (,$(wildcard Makefile.dep))
include Makefile.dep
endif
