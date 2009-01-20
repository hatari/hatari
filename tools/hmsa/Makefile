# Makefile for Hatari MSA

# Include settings
include ../../Makefile.cnf

HMSA_OBJS = hmsa.o floppy.o
HATARI_OBJS = msa.o dim.o st.o file.o unzip.o zip.o
OBJS = $(HMSA_OBJS) $(HATARI_OBJS:%.o=../../src/%.o)

# Additional include directories:
CPPFLAGS += $(SDL_CFLAGS) -I../.. -I../../src/includes -I../../src/uae-cpu

LIBS = -lz

all: hmsa

hmsa: $(OBJS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o hmsa

clean:
	$(RM) *.o hmsa

distclean: clean
	$(RM) *~ *.bak
