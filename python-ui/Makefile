# Makefile for installing Hatari UI
#
# TODO:
# - install separate startup/.desktop script for maemo
# - how to do byte-compile (*.pyc) and optimization (*.pyo)?

# Include settings
include ../Makefile.cnf

prefix ?= /usr

BINPATH  = $(DESTDIR)$(BINDIR)
SCRIPT   = $(BINPATH)/hatariui
DATAPATH = $(DESTDIR)$(DATADIR)/hatariui
ICONPATH = $(prefix)/share/icons/hicolor/32x32/apps
DESKPATH = $(prefix)/share/applications

all:
	@echo "Nothing to build, just do 'make install'"

install:
	$(INSTALL) -d $(BINPATH)
	$(INSTALL_SCRIPT) hatari-console.py $(BINPATH)/
	sed "s%^path=.*$$%path=$(DATAPATH)%" hatariui > $(SCRIPT)
	chmod 755 $(SCRIPT)
	$(INSTALL) -d $(DATAPATH)
	$(INSTALL_SCRIPT) hatariui.py debugui.py $(DATAPATH)/
	$(INSTALL_DATA) config.py dialogs.py hatari.py uihelpers.py $(DATAPATH)/
	$(INSTALL_DATA) README TODO Changelog *.png $(DATAPATH)/
	$(INSTALL) -d $(ICONPATH)
	$(INSTALL_DATA) hatari-icon.png $(ICONPATH)/
	$(INSTALL) -d $(DESKPATH)
	$(INSTALL_DATA) hatariui.desktop $(DESKPATH)/
