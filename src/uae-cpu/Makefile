# Makefile for Hatari's UAE CPU.

# The native C compiler - normaly the same as $(CC) unless you use a cross
# compiler.
HOSTCC = $(CC)

# Set flags passed to the compiler (e.g. optimization flags)
CMPLRFLAGS = -g -O2 -Wall -Wno-unused

# SDL-Library configuration:
SDLLIB = $(shell sdl-config --libs)
SDLFLAGS = $(shell sdl-config --cflags)


CFLAGS = -I. $(CMPLRFLAGS) $(SDLFLAGS)


CPUCSRCS = hatari-glue.c memory.c newcpu.c readcpu.c fpp.c cpustbl.c cpudefs.c

#CPUEMUSRCS = cpufast1.s cpufast2.s cpufast3.s cpufast4.s \
#    cpufast5.s cpufast6.s cpufast7.s cpufast8.s
CPUEMUSRCS = cpuemu1.c cpuemu2.c cpuemu3.c cpuemu4.c \
    cpuemu5.c cpuemu6.c cpuemu7.c cpuemu8.c

CPU_OBS = $(CPUCSRCS:.c=.o) $(CPUEMUSRCS:.c=.o)


all: cpudefs.c cpuemu.c $(CPU_OBS)


clean:
	rm -f *.o gencpu build68k cpuopti cpudefs.c cpustbl.c cputbl.h cpuemu.c

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

#cpuopti: cpuopti.o
#	$(HOSTCC) $(LDFLAGS) -o cpuopti cpuopti.o


# this is for people with low memory (is there a way do do this with a single rule ?)
cpuemu1.o: cpuemu.c
	$(CC) -DPART_1 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu2.o: cpuemu.c
	$(CC) -DPART_2 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu3.o: cpuemu.c
	$(CC) -DPART_3 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu4.o: cpuemu.c
	$(CC) -DPART_4 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu5.o: cpuemu.c
	$(CC) -DPART_5 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu6.o: cpuemu.c
	$(CC) -DPART_6 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu7.o: cpuemu.c
	$(CC) -DPART_7 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@
cpuemu8.o: cpuemu.c
	$(CC) -DPART_8 $(INCLUDES) -c $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $< -o $@


cpufast1.s: cpuemu.c cpuopti
	$(CC) -DPART_1 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp1.s
	./cpuopti <cputmp1.s >$@
	rm cputmp1.s
cpufast2.s: cpuemu.c cpuopti
	$(CC) -DPART_2 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp2.s
	./cpuopti <cputmp2.s >$@
	rm cputmp2.s
cpufast3.s: cpuemu.c cpuopti
	$(CC) -DPART_3 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp3.s
	./cpuopti <cputmp3.s >$@
	rm cputmp3.s
cpufast4.s: cpuemu.c cpuopti
	$(CC) -DPART_4 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp4.s
	./cpuopti <cputmp4.s >$@
	rm cputmp4.s
cpufast5.s: cpuemu.c cpuopti
	$(CC) -DPART_5 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp5.s
	./cpuopti <cputmp5.s >$@
	rm cputmp5.s
cpufast6.s: cpuemu.c cpuopti
	$(CC) -DPART_6 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp6.s
	./cpuopti <cputmp6.s >$@
	rm cputmp6.s
cpufast7.s: cpuemu.c cpuopti
	$(CC) -DPART_7 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp7.s
	./cpuopti <cputmp7.s >$@
	rm cputmp7.s
cpufast8.s: cpuemu.c cpuopti
	$(CC) -DPART_8 $(INCLUDES) -S $(INCDIRS) $(CFLAGS) $(X_CFLAGS) $(DEBUGFLAGS) $(NO_SCHED_CFLAGS) $< -o cputmp8.s
	./cpuopti <cputmp8.s >$@
	rm cputmp8.s


newcpu.o: sysdeps.h hatari-glue.h maccess.h memory.h newcpu.h events.h
