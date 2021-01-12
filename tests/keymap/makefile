
CFLAGS := -O -Wall $$(pkg-config --cflags sdl2)
LIBS   := $$(pkg-config --libs sdl2)

all: checkkeys

checkkeys: checkkeys.c makefile
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

clean:
	$(RM) checkkeys *.[oO] *~
