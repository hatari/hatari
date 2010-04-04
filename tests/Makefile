# Makefile for testing Hatari features

# Include settings
include ../Makefile.cnf


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
