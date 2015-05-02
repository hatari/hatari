# Makefile for testing Hatari (debugger) features
#
# "make":
# - compile tests
#
# "make test":
# - run tests
#
# "make valgrind":
# - run tests under valgrind
#
# "make clean; make CC=gcc-<version> MUDFLAP=1 mudflap":
# - run tests with older GCC version mudflap checks
#   (MudFlap was removed/replaced in GCC 4.9 with AddressSanitizer)
#
# TODO: support AddressSanitizer

# Set the C compiler (e.g. gcc)
CC ?= gcc

# Directory given for 'cmake' i.e. where CMake created the config.h.
# Could also be simply "../.." or "../../build".
CONFIGDIR := $(shell find ../.. -name config.h | head -1 | sed 's%/[^/]*$$%%')

# SDL-Library configuration (compiler flags and linker options) - you normally
# don't have to change this if you have correctly installed the SDL library!
SDL_CFLAGS := $(shell sdl-config --cflags)

# What warnings to use
WARNFLAGS = -Wmissing-prototypes -Wstrict-prototypes -Wsign-compare \
  -Wbad-function-cast -Wcast-qual  -Wpointer-arith -Wwrite-strings -Wall

# Hatari source include directories:
INCFLAGS = -I$(CONFIGDIR) -I../../src/includes -I../../src/uae-cpu \
  -I../../src/debug -I../../src/falcon

# Set extra flags passed to the compiler
CFLAGS := -g -O $(INCFLAGS) $(WARNFLAGS) $(SDL_CFLAGS)


ifneq ($(MUDFLAP),)
# Run-time checks with GCC "mudflap" etc:
# - stack protection
# - checking of pointer accesses (AFAIK works only on x86)
#
# Before build, install mudflap library development package,
# typically it's named either "libmudflap<version>-<gcc-version>-dev"
# (e.g. libmudflap0-4.4-dev in Debian Squeeze) or "libmudflap-devel"
# (e.g. in Fedora).
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
# These tests don't thread, so -fmudflap can be used instead of -fmudflapth
CFLAGS += -fstack-protector-all -fmudflap
LDFLAGS = -fmudflap -lmudflap
endif


# Valgrind options
VALGRIND_OPTS = --leak-check=full --leak-resolution=high \
  --free-fill=0 --error-exitcode=1


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
	for test in $^; do ./$$test; echo "Press <Enter>"; read proceed; done

# Valgrind tests run faster if you compile without Mudflap
valgrind: $(TESTS)
	for test in $^; do \
	valgrind $(VALGRIND_OPTS) ./$$test; \
	echo "Press <Enter>"; read proceed; \
	done

# just run the tests
test: $(TESTS)
	for test in $^; do ./$$test; echo "Press <Enter>"; read proceed; done

# all the real & dummy deps
SOURCEDEPS = ../../src/debug/breakcond.c \
	../../src/debug/debugcpu.c ../../src/debug/history.c \
	../../src/debug/evaluate.c ../../src/debug/symbols.c \
	../../src/str.c test-dummies.c

test-symbols: test-symbols.c $(SOURCEDEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test-evaluate: test-evaluate.c $(SOURCEDEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test-breakcond: test-breakcond.c $(SOURCEDEPS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


clean:
	$(RM) *.o $(TESTS)

distclean: clean
	$(RM) *~ *.bak *.orig
