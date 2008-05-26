#!/usr/bin/env python
#
# Misc common helper classes and functions for the Hatari UI
#
# Copyright (C) 2008 by Eero Tamminen <eerot@sf.net>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gobject


# constants for the UI window
class UInfo:
    version = "v0.6"
    name = "Hatari UI"
    logo = "hatari.png"
    icon = "hatari-icon.png"
    # size reserved for the embedded Hatari window
    width = 640
    height = 400


# auxiliary class to be used with the PasteDialog
class HatariTextInsert():
    def __init__(self, hatari, text):
        self.index = 0
        self.text = text
        self.pressed = False
        self.hatari = hatari
        print "OUTPUT '%s'" % text
        gobject.timeout_add(100, _text_insert_cb, self)

# callback to insert text object to Hatari character at the time, at given interval
def _text_insert_cb(textobj):
    char = textobj.text[textobj.index]
    if textobj.pressed:
        textobj.pressed = False
        textobj.hatari.insert_event("keyrelease %c" % char)
        textobj.index += 1
        if textobj.index >= len(textobj.text):
            del(textobj)
            return False
    else:
        textobj.pressed = True
        textobj.hatari.insert_event("keypress %c" % char)
    return True


def create_button(label, cb, data = None):
    "create_button(label,cb[,data]) -> button widget"
    button = gtk.Button(label)
    if data == None:
        button.connect("clicked", cb)
    else:
        button.connect("clicked", cb, data)
    return button


def create_toggle(label, cb, data = None):
    "create_toggle(label,cb[,data]) -> toggle button widget"
    button = gtk.ToggleButton(label)
    if data == None:
        button.connect("toggled", cb)
    else:
        button.connect("toggled", cb, data)
    return button
