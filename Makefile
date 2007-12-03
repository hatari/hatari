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
	@echo "Did not find config files! I will try to use the default configuration now..."
	cp Makefile-default.cnf Makefile.cnf
	cp config-default.h config.h

hatari.1.gz: doc/hatari.1
	gzip -9 -c doc/hatari.1 > $@


clean:
	$(MAKE) -C src/ clean

distclean:
	$(MAKE) -C src/ distclean
	$(RM) config.cache config.log Makefile.cnf config.h
	$(RM) hatari.1.gz


# Use "make depend" to generate file dependencies:
depend:
	$(MAKE) -C src/ depend

install: all hatari.1.gz
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -d $(DESTDIR)$(DOCDIR)
	$(INSTALL) -d $(DESTDIR)$(DOCDIR)/images
	$(INSTALL_PROGRAM) src/hatari $(DESTDIR)$(BINDIR)/hatari
	$(INSTALL_DATA) src/hatari-icon.bmp $(DESTDIR)$(DATADIR)/hatari-icon.bmp
	if test -f src/tos.img -a \! -f $(DESTDIR)$(DATADIR)/tos.img ; then \
	  $(INSTALL_DATA) src/tos.img $(DESTDIR)$(DATADIR)/tos.img ; \
	fi
	$(INSTALL_DATA) hatari.1.gz $(DESTDIR)$(MANDIR)/
	$(INSTALL_DATA) doc/*.txt doc/*.html $(DESTDIR)$(DOCDIR)/
	$(INSTALL_DATA) doc/images/*.png $(DESTDIR)$(DOCDIR)/images/
