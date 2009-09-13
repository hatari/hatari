# Makefile for testing Hatari features

# Include settings
include ../Makefile.cnf

VALGRIND_OPTS = --leak-check=full --leak-resolution=high \
  --free-fill=0 --error-exitcode=1

# Additional include directories:
INCFLAGS = -I.. -I../src/includes -I../src/uae-cpu \
  -I../src/debug -I../src/falcon

# Set extra flags passed to the compiler
CFLAGS += $(INCFLAGS) $(SDL_CFLAGS)

TESTS = test-breakcond

all: $(TESTS)

valgrind: test-breakcond
	for test in $^; do valgrind $(VALGRIND_OPTS) ./$$test; done

test-breakcond: test-breakcond.c ../src/debug/breakcond.c ../src/str.c 
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) *.o

distclean: clean
	$(RM) *~ *.bak *.orig
