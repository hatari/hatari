# Makefile for Hatari MSA

# Include settings
include ../../Makefile.cnf

HMSA_OBJS = hmsa.o floppy.o
HATARI_OBJS = createBlankImage.o msa.o dim.o st.o file.o unzip.o zip.o
OBJS = $(HMSA_OBJS) $(HATARI_OBJS:%.o=../../src/%.o)

# Additional include directories:
CPPFLAGS += $(SDL_CFLAGS) -I../.. -I../../src/includes -I../../src/debug -I../../src/uae-cpu

all: hmsa

hmsa: $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LIBS) -o hmsa

# NOTE: \x08 in NROFF backspace removal needs GNU sed
readme-hmsa.txt: hmsa.1
	nroff -man -Tutf8 $< | sed 's/.\x08//g' > $@

clean:
	$(RM) *.o hmsa

distclean: clean
	$(RM) *~ *.bak
