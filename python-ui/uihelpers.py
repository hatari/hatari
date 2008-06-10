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

import os
import sys
# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gobject


# leak debugging
#import gc
#gc.set_debug(gc.DEBUG_UNCOLLECTABLE)


# ---------------------
# Hatari UI information

class UInfo:
    """singleton constants for the UI windows,
    one instance is needed to initialize these properly"""
    version = "v0.6"
    name = "Hatari UI"
    logo = "hatari.png"
    icon = "hatari-icon.png"
    # path to the directory where the called script resides
    path = sys.argv[0][:sys.argv[0].rfind(os.path.sep)]
    # size reserved for the embedded Hatari window
    width = 640
    height = 400
    
    def __init__(self, path = None):
        "UIinfo([path]), set suitable paths for resources from CWD and path"
        if path:
            self.path = path
        if not os.path.exists(UInfo.icon):
            UInfo.icon = self._get_path(UInfo.icon)
        if not os.path.exists(UInfo.logo):
            UInfo.logo = self._get_path(UInfo.logo)

    def _get_path(self, filename):
        sep = os.path.sep
        testpath = "%s%s%s" % (self.path, sep, filename)
        if os.path.exists(testpath):
            return testpath


# --------------------------------------------------------
# auxiliary class+callback to be used with the PasteDialog

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


# ----------------------------
# helper functions for buttons

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


# -----------------------------
# Table dialog helper functions

def create_table_dialog(parent, title, rows):
    "create_table_dialog(parent,title,rows) -> (table,dialog)"
    dialog = gtk.Dialog(title, parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
        gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))

    table = gtk.Table(rows, 2) # rows, cols
    table.set_col_spacings(8)
    dialog.vbox.add(table)
    return (table, dialog)

def table_add_entry_row(table, row, label, size = None):
    "table_add_entry_row(table,row,label,[entry size]) -> entry"
    # adds given label to given row in given table
    # returns entry for that line
    label = gtk.Label(label)
    align = gtk.Alignment(1) # right aligned
    align.add(label)
    table.attach(align, 0, 1, row, row+1, gtk.FILL)
    if size:
        entry = gtk.Entry(size)
        entry.set_width_chars(size)
        align = gtk.Alignment(0) # left aligned (default is centered)
        align.add(entry)
        table.attach(align, 1, 2, row, row+1)
    else:
        entry = gtk.Entry()
        table.attach(entry, 1, 2, row, row+1)
    return entry

def table_add_widget_row(table, row, label, widget):
    "table_add_widget_row(table,row,label,widget) -> widget"
    # adds given label right aligned to given row in given table
    # adds given widget to the right column and returns it
    # returns entry for that line
    label = gtk.Label(label)
    align = gtk.Alignment(1)
    align.add(label)
    table.attach(align, 0, 1, row, row+1, gtk.FILL)
    table.attach(widget, 1, 2, row, row+1)
    return widget

def table_add_separator(table, row):
    "table_add_separator(table,row)"
    widget = gtk.HSeparator()
    table.attach(widget, 0, 2, row, row+1, gtk.FILL)
