# Top directory Makefile for Hatari.

# Include settings
include Makefile.cnf


all:
	$(MAKE) -C src/

clean:
	$(MAKE) -C src/ clean

# Use "make depend" to generate file dependencies:
depend:
	$(MAKE) -C src/ depend

install: all
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) -d $(DATADIR)
	$(INSTALL_PROGRAM) src/hatari $(BINDIR)/hatari
	if test -f src/tos.img -a \! -f $(DATADIR)/tos.img ; then \
	  $(INSTALL_DATA) src/tos.img $(DATADIR)/tos.img ; \
	fi
