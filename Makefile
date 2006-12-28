# Top directory Makefile for Hatari.
#
# You can set DESTDIR as temporary installation prefix
# e.g. when creating RPM packages.

# Include settings
-include Makefile.cnf


all:
	$(MAKE) -C src/

# No Makefile configuration available yet? Then use the default file: 
Makefile.cnf:
	 cp Makefile-default.cnf Makefile.cnf


clean:
	$(MAKE) -C src/ clean

distclean:
	$(MAKE) -C src/ distclean
	$(RM) config.cache config.log Makefile.cnf


# Use "make depend" to generate file dependencies:
depend:
	$(MAKE) -C src/ depend

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)
	$(INSTALL_PROGRAM) src/hatari $(DESTDIR)$(BINDIR)/hatari
	$(INSTALL_DATA) src/hatari-icon.bmp $(DESTDIR)$(DATADIR)/hatari-icon.bmp
	if test -f src/tos.img -a \! -f $(DESTDIR)$(DATADIR)/tos.img ; then \
	  $(INSTALL_DATA) src/tos.img $(DESTDIR)$(DATADIR)/tos.img ; \
	fi
