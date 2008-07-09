# Makefile for installing Hatari UI
#
# TODO:
# - do / install .desktop file
# - install separate startup/.desktop script for maemo
# - how to do byte-compile (*.pyc) and optimization (*.pyo)?

# overridable default installation directory
DESTDIR ?= /usr/local

# Debian wants to give DESTDIR as the root.
# add usr/ to paths unless it's /usr/local
# 
ifneq ($(DESTDIR),/usr/local)
	DESTDIR=$(DESTDIR)/usr
endif
BINPATH = $(DESTDIR)/bin
SCRIPT  = $(BINPATH)/hatariui
PKGPATH = $(DESTDIR)/share/hatariui

all:
	@echo "Nothing to build, just do 'make install'"

install:
	install -d $(BINPATH)
	install -m 755 hatari-console.py $(BINPATH)/
	sed "s%^path=.*$$%path=$(PKGPATH)%" hatariui > $(SCRIPT)
	chmod 755 $(SCRIPT)
	install -d $(PKGPATH)
	install -m 755 hatariui.py debugui.py $(PKGPATH)/
	install -m 644 config.py dialogs.py hatari.py uihelpers.py $(PKGPATH)/
	install -m 644 README *.png $(PKGPATH)/
