# Makefile for testing Hatari features

# Set the C compiler (e.g. gcc)
CC = gcc

# SDL-Library configuration (compiler flags and linker options) - you normally
# don't have to change this if you have correctly installed the SDL library!
SDL_CFLAGS := $(shell sdl-config --cflags)
SDL_LIBS := $(shell sdl-config --libs)

# What warnings to use
WARNFLAGS = -Wmissing-prototypes -Wstrict-prototypes -Wsign-compare \
  -Wbad-function-cast -Wcast-qual  -Wpointer-arith -Wwrite-strings -Wall

ifneq ($(MUDFLAP),)
# Run-time checks with GCC "mudflap" etc:
# - stack protection
# - checking of pointer accesses (AFAIK works only on x86)
#
# Before build, install "libmudflap<version>-<gcc-version>-dev"
# package (libmudflap0-4.3-dev in Debian Lenny).
#
# To build, use:
#	make clean; make MUDFLAP=1
#
# To run, use something like (disable sound as it can break things):
#   MUDFLAP_OPTIONS="-viol-gdb" ./hatari --sound off
#
# For more info, see (for now, works properly only for x86 gcc):
#   http://gcc.gnu.org/wiki/Mudflap_Pointer_Debugging
#
RUNCHECKS = -fstack-protector-all -fmudflapth #-fmudflapir
LDRUNCHECKS = -fmudflapth -lmudflap
endif

# Set flags passed to the compiler (e.g. optimization flags)
CFLAGS := -g -O $(WARNFLAGS) $(RUNCHECKS)

# Valgrind options
VALGRIND_OPTS = --leak-check=full --leak-resolution=high \
  --free-fill=0 --error-exitcode=1

# Additional include directories:
INCFLAGS = -I.. -I../src/includes -I../src/uae-cpu \
  -I../src/debug -I../src/falcon

# Set extra flags passed to the compiler
CFLAGS += $(INCFLAGS) $(SDL_CFLAGS) $(LDFLAGS)


TESTS = test-symbols test-evaluate test-breakcond

all: $(TESTS)

# To do mudflap checks, use:
# 	make clean; make MUDFLAP=1 mudflap
#
# "-check-initialization" can be used here because these tests don't
# handle data initialized in external libraries (like Hatari uses
# SDL surface structs initialized by SDL).
# 
# "-print-leaks" can be used because these tests don't utilize external
# libraries which leave their resources behind on exit (like SDL audio
# and X locale stuff).
mudflap: $(TESTS)
	export MUDFLAP_OPTIONS="-viol-gdb -collect-stats -internal-checking -print-leaks -backtrace=8 -check-initialization -wipe-stack -wipe-heap"; \
	for test in $^; do ./$$test; echo "Press <Enter>"; read; done

valgrind: $(TESTS)
	for test in $^; do valgrind $(VALGRIND_OPTS) ./$$test; done


test-symbols: test-symbols.c ../src/debug/symbols.c
	$(CC) $(CFLAGS) -o $@ $^

test-evaluate: test-evaluate.c ../src/debug/evaluate.c \
		../src/debug/debugcpu.c ../src/falcon/dsp.c \
		../src/debug/symbols.c ../src/str.c \
		../src/debug/breakcond.c test-dummies.c
	$(CC) $(CFLAGS) -o $@ $^

test-breakcond: test-breakcond.c ../src/debug/breakcond.c \
		../src/debug/debugcpu.c ../src/falcon/dsp.c \
		../src/debug/symbols.c ../src/str.c \
		../src/debug/evaluate.c test-dummies.c
	$(CC) $(CFLAGS) -o $@ $^


clean:
	$(RM) *.o $(TESTS)

distclean: clean
	$(RM) *~ *.bak *.orig
