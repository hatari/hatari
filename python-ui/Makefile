# Makefile for installing Hatari UI
#
# TODO:
# - install separate startup/.desktop script for maemo
# - how to do byte-compile (*.pyc) and optimization (*.pyo)?

# Include settings
include ../Makefile.cnf

prefix ?= /usr

SCRIPT   = $(BINDIR)/hatariui
DATAPATH = $(DATADIR)/hatariui
ICONPATH = $(prefix)/share/icons/hicolor/32x32/apps
DESKPATH = $(prefix)/share/applications

all:
	@echo "Nothing to build, just do 'make install'"

install:
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL_SCRIPT) hatari-console.py $(DESTDIR)$(BINDIR)/
	sed "s%^path=.*$$%path=$(DATAPATH)%" hatariui > $(DESTDIR)$(SCRIPT)
	chmod 755 $(DESTDIR)$(SCRIPT)
	$(INSTALL) -d $(DESTDIR)$(DATAPATH)
	$(INSTALL_SCRIPT) hatariui.py debugui.py $(DESTDIR)$(DATAPATH)/
	$(INSTALL_DATA) config.py dialogs.py hatari.py uihelpers.py $(DESTDIR)$(DATAPATH)/
	$(INSTALL_DATA) README TODO release-notes.txt *.png $(DESTDIR)$(DATAPATH)/
	$(INSTALL) -d $(DESTDIR)$(ICONPATH)
	$(INSTALL_DATA) hatari-icon.png $(DESTDIR)$(ICONPATH)/
	$(INSTALL) -d $(DESTDIR)$(DESKPATH)
	$(INSTALL_DATA) hatariui.desktop $(DESTDIR)$(DESKPATH)/
