# Top directory Makefile for Hatari.
#
# You can set DESTDIR as temporary installation prefix
# e.g. when creating RPM packages.

# Include settings
-include Makefile.cnf


all: Makefile.cnf config.h
	$(MAKE) -C src/
	$(MAKE) -C tools/hmsa/

# Makefile.cnf doesn't exist or is older than Makefile.default.cnf?
Makefile.cnf: Makefile-default.cnf
	@echo "Trying to use the default Makefile configuration..."
	@if [ -f Makefile.cnf ]; then \
		echo "ERROR: Makefile.cnf exists already, but is older than the default"; \
		echo "(just remove it if you haven't changed it)."; \
		exit 1; \
	elif [ x$(findstring indows,$(OS)) = x"indows" ]; then \
		cp -v Makefile-MinGW.cnf Makefile.cnf; \
	else \
		cp -v Makefile-default.cnf Makefile.cnf; \
	fi

# config.h doesn't exist or is older than config-default.h?
config.h: config-default.h
	@echo "Trying to use the default config.h configuration..."
	@if [ -f config.h ]; then \
		echo "ERROR: config.h exists already and is older than the default"; \
		echo "(just remove it if you haven't changed it, or re-run configure)."; \
		exit 1; \
	else \
		cp -v config-default.h config.h; \
	fi

hatari.1.gz: doc/hatari.1
	gzip -9 -c $<  >  $@

hmsa.1.gz: tools/hmsa/hmsa.1
	gzip -9 -c $<  >  $@


clean:
	$(MAKE) -C src/ clean
	$(MAKE) -C tools/hmsa/ clean

distclean:
	$(MAKE) -C src/ distclean
	$(MAKE) -C tools/hmsa/ distclean
	$(RM) config.cache config.log Makefile.cnf config.h
	$(RM) hatari.1.gz hmsa.1.gz


# Use "make depend" to generate file dependencies:
depend:
	$(MAKE) -C src/ depend

install: all hatari.1.gz hmsa.1.gz
	@if [ "x$(INSTALL)" = "x" ]; then \
		echo; \
		echo "ERROR: Hatari isn't yet configured for installation."; \
		exit 1; \
	fi
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)
	$(INSTALL) -d $(DESTDIR)$(MANDIR)
	$(INSTALL) -d $(DESTDIR)$(DOCDIR)
	$(INSTALL) -d $(DESTDIR)$(DOCDIR)/images
	$(INSTALL_PROGRAM) src/hatari $(DESTDIR)$(BINDIR)/hatari
	$(INSTALL_PROGRAM) tools/hmsa/hmsa $(DESTDIR)$(BINDIR)/hmsa
	$(INSTALL_SCRIPT) tools/zip2st.sh $(DESTDIR)$(BINDIR)/zip2st
	$(INSTALL_DATA) src/hatari-icon.bmp $(DESTDIR)$(DATADIR)/hatari-icon.bmp
	if test -f src/tos.img -a \! -f $(DESTDIR)$(DATADIR)/tos.img ; then \
	  $(INSTALL_DATA) src/tos.img $(DESTDIR)$(DATADIR)/tos.img ; \
	fi
	$(INSTALL_DATA) hatari.1.gz $(DESTDIR)$(MANDIR)/
	$(INSTALL_DATA) hmsa.1.gz $(DESTDIR)$(MANDIR)/
	$(INSTALL_DATA) doc/*.txt doc/*.html $(DESTDIR)$(DOCDIR)/
	$(INSTALL_DATA) doc/images/*.png $(DESTDIR)$(DOCDIR)/images/
	$(MAKE) -C python-ui/ install
