# Makefile for Hatari's UAE CPU.

# Include settings
include ../../Makefile.cnf


CFLAGS += -I. -I../.. -I../includes -I../debug -I../falcon $(CPPFLAGS) $(SDL_CFLAGS)
# disable several warnings for the CPU emu code as it comes from elsewhere
QUIETCFLAGS = $(CFLAGS) -Wno-unused -Wno-sign-compare -Wno-shadow


CPUCSRCS = hatari-glue.c memory.c newcpu.c readcpu.c fpp.c

CPUGENOBS = cpuemu1.o cpuemu2.o cpuemu3.o cpuemu4.o \
            cpuemu5.o cpuemu6.o cpuemu7.o cpuemu8.o \
            cpudefs.o cpustbl.o

CPU_OBS = $(CPUCSRCS:.c=.o) $(CPUGENOBS)


all: cpu68k.a

cpu68k.a: $(CPU_OBS)
	$(AR) cru $@ $^
	$(RANLIB) $@

build68k: build68k.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

cpudefs.c: build68k table68k
	./build68k <table68k >cpudefs.c

cpudefs-host.o: cpudefs.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

readcpu-host.o: readcpu.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

gencpu.o: gencpu.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

gencpu: gencpu.o readcpu-host.o cpudefs-host.o
	$(HOSTCC) $(HOSTLDFLAGS) -o $@ $^

cpuemu.c: gencpu
	./gencpu


cpustbl.c: cpuemu.c
cputbl.h: cpuemu.c


# this is for people with low memory (is there a way do do this with a single rule ?)
cpuemu1.o: cpuemu.c
	$(CC) -DPART_1 -c $(QUIETCFLAGS) $< -o $@
cpuemu2.o: cpuemu.c
	$(CC) -DPART_2 -c $(QUIETCFLAGS) $< -o $@
cpuemu3.o: cpuemu.c
	$(CC) -DPART_3 -c $(QUIETCFLAGS) $< -o $@
cpuemu4.o: cpuemu.c
	$(CC) -DPART_4 -c $(QUIETCFLAGS) $< -o $@
cpuemu5.o: cpuemu.c
	$(CC) -DPART_5 -c $(QUIETCFLAGS) $< -o $@
cpuemu6.o: cpuemu.c
	$(CC) -DPART_6 -c $(QUIETCFLAGS) $< -o $@
cpuemu7.o: cpuemu.c
	$(CC) -DPART_7 -c $(QUIETCFLAGS) $< -o $@
cpuemu8.o: cpuemu.c
	$(CC) -DPART_8 -c $(QUIETCFLAGS) $< -o $@


clean:
	$(RM) *.o gencpu build68k cpu68k.a

distclean:	clean
	$(RM) cpudefs.c cpustbl.c cputbl.h cpuemu.c
	$(RM) *~ *.bak *.orig Makefile.dep


# Use "make depend" to generate file dependencies:
Makefile.dep: Makefile ../Makefile
	$(CC) -M $(CFLAGS) $(CPUCSRCS) > Makefile.dep

depend: Makefile.dep

-include Makefile.dep
