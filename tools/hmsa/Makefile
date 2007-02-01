# Makefile for MSA

HMSA_OBJS = hmsa.o floppy.o
HATARI_OBJS = msa.o dim.o st.o file.o unzip.o zip.o
OBJS = $(HMSA_OBJS) $(HATARI_OBJS:%.o=../../src/%.o)

CFLAGS = -g -O -Wall -I../../src/includes \
	$(shell sdl-config --cflags)

LIBS = -lz

all: hmsa

hmsa: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o hmsa

clean:
	$(RM) *.o hmsa
