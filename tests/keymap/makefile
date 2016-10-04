# SDL v1 build: "make SDL=sdl"
# SDL v2 build: "make SDL=sdl2"

SDL    ?= sdl
CFLAGS := -O -Wall $$(pkg-config --cflags $(SDL))
LIBS   := $$(pkg-config --libs $(SDL))

all: listkeys checkkeys

checkkeys: checkkeys.c Makefile
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

listkeys: listkeys.c Makefile
	$(CC) -o $@ $(CFLAGS) $< $(LIBS)

clean:
	$(RM) listkeys checkkeys *.[oO] *~
