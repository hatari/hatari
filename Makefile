# Top directory Makefile for Hatari.

# Include settings
include Makefile.cnf


all:
	$(MAKE) -C src/


clean:
	$(MAKE) -C src/ clean

distclean:
	$(MAKE) -C src/ distclean
	$(RM) config.cache config.log


# Use "make depend" to generate file dependencies:
depend:
	$(MAKE) -C src/ depend

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)
	$(INSTALL_PROGRAM) src/hatari $(DESTDIR)$(BINDIR)/hatari
	if test -f src/tos.img -a \! -f $(DESTDIR)$(DATADIR)/tos.img ; then \
	  $(INSTALL_DATA) src/tos.img $(DESTDIR)$(DATADIR)/tos.img ; \
	fi
