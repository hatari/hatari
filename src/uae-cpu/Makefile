# Makefile for Hatari's UAE CPU.

# Include settings
include ../../Makefile.cnf


CFLAGS += -I. $(SDL_CFLAGS)


CPUCSRCS = hatari-glue.c memory.c newcpu.c readcpu.c fpp.c cpustbl.c cpudefs.c

CPUEMUSRCS = cpuemu1.c cpuemu2.c cpuemu3.c cpuemu4.c \
             cpuemu5.c cpuemu6.c cpuemu7.c cpuemu8.c

CPU_OBS = $(CPUCSRCS:.c=.o) $(CPUEMUSRCS:.c=.o)


all: cpudefs.c cpuemu.c $(CPU_OBS)


clean:
	rm -f *.o gencpu build68k cpudefs.c cpustbl.c cputbl.h cpuemu.c

build68k: build68k.c
	$(HOSTCC) $(CFLAGS) -o build68k build68k.c

cpudefs.c: ./build68k ./table68k
	./build68k <./table68k >cpudefs.c

gencpu.o: gencpu.c
	$(HOSTCC) $(CFLAGS) -c -o gencpu.o gencpu.c

gencpu: gencpu.o readcpu.c cpudefs.c
	$(HOSTCC) $(CFLAGS) $(LDFLAGS) -o gencpu gencpu.o readcpu.c cpudefs.c

cpuemu.c: gencpu
	./gencpu

cpustbl.c: cpuemu.c
cputbl.h: cpuemu.c


# this is for people with low memory (is there a way do do this with a single rule ?)
cpuemu1.o: cpuemu.c
	$(CC) -DPART_1 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu2.o: cpuemu.c
	$(CC) -DPART_2 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu3.o: cpuemu.c
	$(CC) -DPART_3 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu4.o: cpuemu.c
	$(CC) -DPART_4 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu5.o: cpuemu.c
	$(CC) -DPART_5 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu6.o: cpuemu.c
	$(CC) -DPART_6 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu7.o: cpuemu.c
	$(CC) -DPART_7 -c $(CPPFLAGS) $(CFLAGS) $< -o $@
cpuemu8.o: cpuemu.c
	$(CC) -DPART_8 -c $(CPPFLAGS) $(CFLAGS) $< -o $@


newcpu.o: sysdeps.h hatari-glue.h maccess.h memory.h newcpu.h events.h
