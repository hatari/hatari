#!/usr/bin/env python
#
# Misc common helper classes and functions for the Hatari UI
#
# Copyright (C) 2008-2011 by Eero Tamminen <eerot at berlios>
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
    version = "v1.0"
    name = "Hatari UI"
    logo = "hatari.png"
    icon = "hatari-icon.png"
    copyright = "UI copyright (C) 2008-2011 by Eero Tamminen"

    # path to the directory where the called script resides
    path = os.path.dirname(sys.argv[0])
    
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
# functions for showing HTML files

class UIHelp:
    def __init__(self):
        """determine HTML viewer and where docs are"""
        self._view = self.get_html_viewer()
        self._path = self.get_doc_path()

    def get_html_viewer(self):
        """return name of html viewer or None"""
        path = self.get_binary_path("xdg-open")
        if path:
            return path
        path = self.get_binary_path("firefox")
        if path:
            return path
        return None
        
    def get_binary_path(self, name):
        """return true if given binary is in path"""
        # could also try running the binary with "--version" arg
        # and check the exec return value
        if os.sys.platform == "win32":
            splitter = ';'
        else:
            splitter = ':'
        for i in os.environ['PATH'].split(splitter):
                fname = os.path.join(i, name)
                if os.access(fname, os.X_OK) and not os.path.isdir(fname):
                    return fname
        return None

    def get_doc_path(self):
        """return path or URL to Hatari docs or None"""
        # first try whether there are local Hatari docs in standard place
        # for this Hatari/UI version
        sep = os.sep
        path = self.get_binary_path("hatari")
        path = sep.join(path.split(sep)[:-2]) # remove "bin/hatari"
        path = path + sep + "share" + sep + "doc" + sep + "hatari" + sep
        if os.path.exists(path + "manual.html"):
            return path
        # if not, point to latest Hatari HG version docs
        print("WARNING: Hatari manual not found at:", path + "manual.html")
        return "http://hg.berlios.de/repos/hatari/raw-file/tip/doc/"

    def set_mainwin(self, widget):
        self.mainwin = widget

    def view_url(self, url, name):
        """view given URL or file path, or error use 'name' as its name"""
        if self._view and "://" in url or os.path.exists(url):
            print("RUN: '%s' '%s'" % (self._view, url))
            os.spawnlp(os.P_NOWAIT, self._view, self._view, url)
            return
        if not self._view:
            msg = "Cannot view %s, HTML viewer missing" % name
        else:
            msg = "Cannot view %s,\n'%s' file is missing" % (name, url)
        from dialogs import ErrorDialog
        ErrorDialog(self.mainwin).run(msg)

    def view_hatari_manual(self, dummy=None):
        self.view_url(self._path + "manual.html", "Hatari manual")

    def view_hatari_compatibility(self, dummy=None):
        self.view_url(self._path + "compatibility.html", "Hatari compatibility list")

    def view_hatari_releasenotes(self, dummy=None):
        self.view_url(self._path + "release-notes.txt", "Hatari release notes")

    def view_hatari_todo(self, dummy=None):
        self.view_url(self._path + "todo.txt", "Hatari TODO items")

    def view_hatari_bugs(self, dummy=None):
        self.view_url("http://developer.berlios.de/bugs/?group_id=10436", "Hatari bug tracker")

    def view_hatari_mails(self, dummy=None):
        self.view_url("http://developer.berlios.de/mail/?group_id=10436", "Hatari mailing lists")

    def view_hatari_repository(self, dummy=None):
        self.view_url("http://hg.berlios.de/repos/hatari", "latest Hatari changes")

    def view_hatari_authors(self, dummy=None):
        self.view_url(self._path + "authors.txt", "Hatari authors")

    def view_hatari_page(self, dummy=None):
        self.view_url("http://hatari.berlios.de/", "Hatari home page")

    def view_hatariui_page(self, dummy=None):
        self.view_url("http://koti.mbnet.fi/tammat/hatari/hatari-ui.shtml", "Hatari UI home page")


# --------------------------------------------------------
# auxiliary class+callback to be used with the PasteDialog

class HatariTextInsert:
    def __init__(self, hatari, text):
        self.index = 0
        self.text = text
        self.pressed = False
        self.hatari = hatari
        print("OUTPUT '%s'" % text)
        gobject.timeout_add(100, _text_insert_cb, self)

# callback to insert text object to Hatari character at the time
# (first key down, on next call up), at given interval
def _text_insert_cb(textobj):
    char = textobj.text[textobj.index]
    if char == ' ':
        # white space gets stripped, use scancode instead
        char = "57"
    if textobj.pressed:
        textobj.pressed = False
        textobj.hatari.insert_event("keyup %s" % char)
        textobj.index += 1
        if textobj.index >= len(textobj.text):
            del(textobj)
            return False
    else:
        textobj.pressed = True
        textobj.hatari.insert_event("keydown %s" % char)
    # call again
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

def create_toolbutton(stock_id, cb, data = None):
    "create_toolbutton(stock_id,cb[,data]) -> toolbar button with stock icon+label"
    button = gtk.ToolButton(stock_id)
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

def create_table_dialog(parent, title, rows, cols, oktext = gtk.STOCK_APPLY):
    "create_table_dialog(parent,title,rows, cols, oktext) -> (table,dialog)"
    dialog = gtk.Dialog(title, parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        (oktext,  gtk.RESPONSE_APPLY,
        gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))

    table = gtk.Table(rows, cols)
    table.set_data("col_offset", 0)
    table.set_col_spacings(8)
    dialog.vbox.add(table)
    return (table, dialog)

def table_set_col_offset(table, offset):
    "set column offset for successive table_* ops on given table"
    table.set_data("col_offset", offset)

def table_add_entry_row(table, row, label, size = None):
    "table_add_entry_row(table,row,label,[entry size]) -> entry"
    # add given label to given row in given table
    # return entry for that line
    label = gtk.Label(label)
    align = gtk.Alignment(1) # right aligned
    align.add(label)
    col = table.get_data("col_offset")
    table.attach(align, col, col+1, row, row+1, gtk.FILL)
    col += 1
    if size:
        entry = gtk.Entry(size)
        entry.set_width_chars(size)
        align = gtk.Alignment(0) # left aligned (default is centered)
        align.add(entry)
        table.attach(align, col, col+1, row, row+1)
    else:
        entry = gtk.Entry()
        table.attach(entry, col, col+1, row, row+1)
    return entry

def table_add_widget_row(table, row, label, widget, fullspan = False):
    "table_add_widget_row(table,row,label,widget) -> widget"
    # add given label right aligned to given row in given table
    # add given widget to the right column and returns it
    # return entry for that line
    if fullspan:
        col = 0
    else:
        col = table.get_data("col_offset")
    if label:
        label = gtk.Label(label)
        align = gtk.Alignment(1)
        align.add(label)
        table.attach(align, col, col+1, row, row+1, gtk.FILL)
    if fullspan:
        col = table.get_data("col_offset")
        table.attach(widget, 1, col+2, row, row+1)
    else:
        table.attach(widget, col+1, col+2, row, row+1)
    return widget

def table_add_radio_rows(table, row, label, texts, cb = None):
    "table_add_radio_rows(table,row,label,texts[,cb]) -> [radios]"
    # - add given label right aligned to given row in given table
    # - create/add radio buttons with given texts to next row, set
    #   the one given as "active" as active and set 'cb' as their
    #   "toggled" callback handler
    # - return array or radiobuttons
    label = gtk.Label(label)
    align = gtk.Alignment(1)
    align.add(label)
    col = table.get_data("col_offset")
    table.attach(align, col, col+1, row, row+1, gtk.FILL)

    radios = []
    radio = None
    box = gtk.VBox()
    for text in texts:
        radio = gtk.RadioButton(radio, text)
        if cb:
            radio.connect("toggled", cb, text)
        radios.append(radio)
        box.add(radio)
    table.attach(box, col+1, col+2, row, row+1)
    return radios

def table_add_separator(table, row):
    "table_add_separator(table,row)"
    widget = gtk.HSeparator()
    endcol = table.get_data("n-columns")
    # separator for whole table width
    table.attach(widget, 0, endcol, row, row+1, gtk.FILL)


# -----------------------------
# File selection helpers

def get_open_filename(title, parent, path = None):
    buttons = (gtk.STOCK_OK, gtk.RESPONSE_OK, gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL)
    fsel = gtk.FileChooserDialog(title, parent, gtk.FILE_CHOOSER_ACTION_OPEN, buttons)
    fsel.set_local_only(True)
    if path:
        fsel.set_filename(path)
    if fsel.run() == gtk.RESPONSE_OK:
        filename = fsel.get_filename()
    else:
        filename = None
    fsel.destroy()
    return filename

def get_save_filename(title, parent, path = None):
    buttons = (gtk.STOCK_OK, gtk.RESPONSE_OK, gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL)
    fsel = gtk.FileChooserDialog(title, parent, gtk.FILE_CHOOSER_ACTION_SAVE, buttons)
    fsel.set_local_only(True)
    fsel.set_do_overwrite_confirmation(True)
    if path:
        fsel.set_filename(path)
        if not os.path.exists(path):
            # above set only folder, this is needed to set
            # the file name when the file doesn't exist
            fsel.set_current_name(os.path.basename(path))
    if fsel.run() == gtk.RESPONSE_OK:
        filename = fsel.get_filename()
    else:
        filename = None
    fsel.destroy()
    return filename


# File selection button with eject button
class FselAndEjectFactory:
    def __init__(self):
        pass

    def get(self, label, path, filename, action):
        "returns file selection button and box having that + eject button"
        fsel = gtk.FileChooserButton(label)
        # Hatari cannot access URIs
        fsel.set_local_only(True)
        fsel.set_width_chars(12)
        fsel.set_action(action)
        if filename:
            fsel.set_filename(filename)
        elif path:
            fsel.set_current_folder(path)
        eject = create_button("Eject", self._eject, fsel)

        box = gtk.HBox()
        box.pack_start(fsel)
        box.pack_start(eject, False, False)
        return (fsel, box)

    def _eject(self, widget, fsel):
        fsel.unselect_all()


# Gtk is braindead, there's no way to set a default filename
# for file chooser button unless it already exists
# - set_filename() works only for files that already exist
# - set_current_name() works only for SAVE action,
#   but file chooser button doesn't support that
# i.e. I had to do my own (less nice) container widget...
class FselEntry:
    def __init__(self, parent, validate = None, data = None):
        self._parent = parent
        self._validate = validate
        self._validate_data = data
        entry = gtk.Entry()
        entry.set_width_chars(12)
        entry.set_editable(False)
        hbox = gtk.HBox()
        hbox.add(entry)
        button = create_button("Select...", self._select_file_cb)
        hbox.pack_start(button, False, False)
        self._entry = entry
        self._hbox = hbox
    
    def _select_file_cb(self, widget):
        fname = self._entry.get_text()
        while True:
            fname = get_save_filename("Select file", self._parent, fname)
            if not fname:
                # assume cancel
                return
            if self._validate:
                # filename needs validation and is valid?
                if not self._validate(self._validate_data, fname):
                    continue
            self._entry.set_text(fname)
            return
    
    def set_filename(self, fname):
        self._entry.set_text(fname)
        
    def get_filename(self):
        return self._entry.get_text()

    def get_container(self):
        return self._hbox
