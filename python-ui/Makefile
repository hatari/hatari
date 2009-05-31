# Makefile for installing Hatari UI
#
# TODO:
# - install separate startup/.desktop script for maemo
# - how to do byte-compile (*.pyc) and optimization (*.pyo)?

# Include settings
-include ../Makefile.cnf

# overridable installation directory prefix
DESTDIR ?= /usr/local

# Debian wants to give DESTDIR as the root.
# add usr/ to paths unless it's /usr/local
# 
ifneq ($(DESTDIR),/usr/local)
	DESTDIR=$(DESTDIR)/usr
endif
BINPATH  = $(DESTDIR)/bin
SCRIPT   = $(BINPATH)/hatariui
DATAPATH = $(DESTDIR)/share/hatariui
ICONPATH = $(DESTDIR)/share/icons/hicolor/32x32/apps
DESKPATH = $(DESTDIR)/share/applications

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
