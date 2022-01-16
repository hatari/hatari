
CFLAGS := -O -Wall $$(pkg-config --cflags sdl2)
LIBS   := $$(pkg-config --libs sdl2)

all: listkeys checkkeys

checkkeys: checkkeys.c makefile
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

listkeys: listkeys.c makefile
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

clean:
	$(RM) listkeys checkkeys *.[oO] *~
