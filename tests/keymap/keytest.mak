# A sozobon makefile for keytest

CFLAGS = -e -v -O
LDFLAGS = -s min_s.o
LOADLIBES = xaesfast

keytest.prg: keytest.c
