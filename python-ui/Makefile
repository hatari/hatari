# Makefile for installing Hatari UI
#
# TODO:
# - install separate startup/.desktop script for maemo
# - how to do byte-compile (*.pyc) and optimization (*.pyo)?

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
	install -d $(BINPATH)
	install -m 755 hatari-console.py $(BINPATH)/
	sed "s%^path=.*$$%path=$(DATAPATH)%" hatariui > $(SCRIPT)
	chmod 755 $(SCRIPT)
	install -d $(DATAPATH)
	install -m 755 hatariui.py debugui.py $(DATAPATH)/
	install -m 644 config.py dialogs.py hatari.py uihelpers.py $(DATAPATH)/
	install -m 644 README *.png $(DATAPATH)/
	install -d $(ICONPATH)
	install -m 644 hatari-icon.png $(ICONPATH)/
	install -d $(DESKPATH)
	install -m 644 hatariui.desktop $(DESKPATH)/
