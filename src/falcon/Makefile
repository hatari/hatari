# Makefile for Hatari's Falcon support.

# Include settings
include ../../Makefile.cnf

# Additional include directories:
INCFLAGS = -I../includes -I../uae-cpu $(CPPFLAGS)


# Set flags passed to the compiler (e.g. optimization flags)
#CFLAGS := -g -O2 -Wall


CFLAGS += -DDATADIR=\"$(DATADIR)\" $(INCFLAGS) $(SDL_CFLAGS)



FALCSRCS = videl.c hostscreen.c

FALCOBJS = $(FALCSRCS:.c=.o)


all: $(FALCOBJS)


clean:
	$(RM) *.o

distclean: clean
	$(RM) Makefile.dep *~ *.bak *.orig


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile
	$(CC) -M $(CFLAGS) $(FALCSRCS) > Makefile.dep

depend: Makefile.dep

-include Makefile.dep
