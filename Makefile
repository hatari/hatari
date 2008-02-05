# Top directory Makefile for Hatari.
#
# You can set DESTDIR as temporary installation prefix
# e.g. when creating RPM packages.

# Include settings
-include Makefile.cnf


all: Makefile.cnf config.h
	$(MAKE) -C src/

# Makefile.cnf doesn't exist or is older than Makefile.default.cnf?
Makefile.cnf: Makefile-default.cnf
	@echo "Trying to use the default Makefile configuration..."
	@if [ -f Makefile.cnf ]; then \
		echo "ERROR: Makefile.cnf exists already and is older (remove if unchanged)"; \
		exit 1; \
	else \
		cp -v Makefile-default.cnf Makefile.cnf; \
	fi

# config.h doesn't exist or is older than config-default.h?
config.h: config-default.h
	@echo "Trying to use the default config.h configuration..."
	@if [ -f config.h ]; then \
		echo "ERROR: config.h exists already and is older (remove if unchanged)"; \
		exit 1; \
	else \
		cp -v config-default.h config.h; \
	fi

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
	@if [ "x$(INSTALL)" = "x" ]; then \
		echo; \
		echo "*** Hatari was not configured for installation. ***"; \
		exit 1; \
	fi
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
