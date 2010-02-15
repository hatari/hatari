# Makefile for testing Hatari features

# Include settings
include ../Makefile.cnf

VALGRIND_OPTS = --leak-check=full --leak-resolution=high \
  --free-fill=0 --error-exitcode=1

# Additional include directories:
INCFLAGS = -I.. -I../src/includes -I../src/uae-cpu \
  -I../src/debug -I../src/falcon

# check for stack overwrites
DEBUGFLAGS = -fstack-protector

# Set extra flags passed to the compiler
CFLAGS += $(DEBUGFLAGS) $(INCFLAGS) $(SDL_CFLAGS)

TESTS = test-symbols test-evaluate test-breakcond

all: $(TESTS)

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
