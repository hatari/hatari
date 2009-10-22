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

TESTS = test-evaluate test-breakcond

all: $(TESTS)

valgrind: $(TESTS)
	for test in $^; do valgrind $(VALGRIND_OPTS) ./$$test; done

test-evaluate: test-evaluate.c ../src/debug/evaluate.c
	$(CC) $(CFLAGS) -o $@ $^

test-breakcond: test-breakcond.c ../src/debug/breakcond.c ../src/debug/evaluate.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) *.o

distclean: clean
	$(RM) *~ *.bak *.orig
