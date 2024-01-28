#
# Misc common helper classes and functions for the Hatari UI
#
# Copyright (C) 2008-2023 by Eero Tamminen
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
import gi
# use correct version of gtk
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
from gi.repository import GLib


# leak debugging
#import gc
#gc.set_debug(gc.DEBUG_UNCOLLECTABLE)


# ---------------------
# Hatari UI information

class UInfo:
    """singleton constants for the UI windows,
    one instance is needed to initialize these properly"""
    version = "v1.4"
    name = "Hatari UI"
    logo = "hatari-logo.png"
    # TODO: use share/icons/hicolor/*/apps/hatari.png instead
    icon = "hatari-icon.png"
    copyright = "Python/Gtk UI copyright (C) 2008-2022 by Eero Tamminen"

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
        self._path, self._uipath = self.get_doc_path()

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

        docpath = path + "/share/doc/hatari/"
        if not os.path.exists(docpath + "manual.html"):
            print("WARNING: using Hatari website URLs, Hatari 'manual.html' not found in: %s" % docpath)
            docpath = "https://hatari.tuxfamily.org/doc/"

        uipath = path + "/share/doc/hatari/hatariui/"
        if not os.path.exists(uipath + "README"):
            print("WARNING: Using Hatari UI Git URLs, Hatari UI 'README' not found in: %s" % uipath)
            uipath = "https://git.tuxfamily.org/hatari/hatari.git/plain/python-ui/"

        return docpath, uipath

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

    def view_hatariui_page(self, dummy=None):
        self.view_url(self._path + "hatari-ui.html", "Hatari UI information")

    def view_hatariui_releasenotes(self, dummy=None):
        self.view_url(self._uipath + "release-notes.txt", "Hatari UI release notes")

    def view_hatari_bugs(self, dummy=None):
        self.view_url(self._path + "bugs.txt", "Hatari bugs")

    def view_hatari_todo(self, dummy=None):
        self.view_url(self._path + "todo.txt", "Hatari TODO items")

    def view_hatari_authors(self, dummy=None):
        self.view_url(self._path + "authors.txt", "Hatari authors")

    def view_hatari_mails(self, dummy=None):
        self.view_url("http://hatari.tuxfamily.org/contact.html", "Hatari mailing lists")

    def view_hatari_repository(self, dummy=None):
        self.view_url("https://git.tuxfamily.org/hatari/hatari.git/", "latest Hatari changes")

    def view_hatari_page(self, dummy=None):
        self.view_url("http://hatari.tuxfamily.org/", "Hatari home page")


# --------------------------------------------------------
# auxiliary class+callback to be used with the PasteDialog

class HatariTextInsert:
    def __init__(self, hatari, text):
        self.index = 0
        self.text = text
        self.pressed = False
        self.hatari = hatari
        print("OUTPUT '%s'" % text)
        GLib.timeout_add(100, _text_insert_cb, self)

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
    button = Gtk.Button(label)
    if data == None:
        button.connect("clicked", cb)
    else:
        button.connect("clicked", cb, data)
    return button

def create_toolbutton(stock_id, cb, data = None):
    "create_toolbutton(stock_id,cb[,data]) -> toolbar button with stock icon+label"
    button = Gtk.ToolButton(stock_id)
    if data == None:
        button.connect("clicked", cb)
    else:
        button.connect("clicked", cb, data)
    return button

def create_toggle(label, cb, data = None):
    "create_toggle(label,cb[,data]) -> toggle button widget"
    button = Gtk.ToggleButton(label)
    if data == None:
        button.connect("toggled", cb)
    else:
        button.connect("toggled", cb, data)
    return button


# -----------------------------
# Table dialog helper functions
#
# TODO: rewrite to use Gtk.Grid instead of Gtk.Table

def create_table_dialog(parent, title, rows, cols, oktext = Gtk.STOCK_APPLY):
    "create_table_dialog(parent,title,rows, cols, oktext) -> (table,dialog)"
    dialog = Gtk.Dialog(title, parent,
        Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
        (oktext,  Gtk.ResponseType.APPLY,
        Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL))

    table = Gtk.Table(rows, cols)
    table.set_col_spacings(8)
    dialog.vbox.add(table)
    return (table, dialog)

def table_add_entry_row(table, row, col, label, size = None):
    "table_add_entry_row(table,row,col,label[,entry size]) -> entry"
    # add given label to given row in given table
    # return entry for that line
    label = Gtk.Label(label=label, halign=Gtk.Align.END)
    table.attach(label, col, col+1, row, row+1, Gtk.AttachOptions.FILL)
    col += 1
    if size:
        entry = Gtk.Entry(max_length=size, width_chars=size, halign=Gtk.Align.START)
        table.attach(entry, col, col+1, row, row+1)
    else:
        entry = Gtk.Entry()
        table.attach(entry, col, col+1, row, row+1)
    return entry

def table_add_widget_row(table, row, col, label, widget, fullspan = False):
    "table_add_widget_row(table,row,col,label,widget[,fullspan]) -> widget"
    # add given label right aligned to given row in given table
    # add given widget to the right column and return it
    if label:
        if fullspan:
            lcol = 0
        else:
            lcol = col
        label = Gtk.Label(label=label, halign=Gtk.Align.END)
        table.attach(label, lcol, lcol+1, row, row+1, Gtk.AttachOptions.FILL)
    if fullspan:
        table.attach(widget, 1, col+2, row, row+1)
    else:
        table.attach(widget, col+1, col+2, row, row+1)
    return widget

def table_add_combo_row(table, row, col, label, texts, cb = None, data = None):
    "table_add_combo_row(table,row,col,label,texts[,cb]) -> combo"
    # - add given label right aligned to given row in given table
    # - create/add combo box with given texts to right column and return it
    combo = Gtk.ComboBoxText()
    for text in texts:
        combo.append_text(text)
    if cb:
        combo.connect("changed", cb, data)
    return table_add_widget_row(table, row, col, label, combo, True)

def table_add_radio_rows(table, row, col, label, texts, cb = None):
    "table_add_radio_rows(table,row,col,label,texts[,cb]) -> [radios]"
    # - add given label right aligned to given row in given table
    # - create/add radio buttons with given texts to next row, set
    #   the one given as "active" as active and set 'cb' as their
    #   "toggled" callback handler
    # - return array or radiobuttons
    label = Gtk.Label(label=label, halign=Gtk.Align.END)
    table.attach(label, col, col+1, row, row+1)

    radios = []
    radio = None
    box = Gtk.VBox()
    for text in texts:
        radio = Gtk.RadioButton(group=radio, label=text)
        if cb:
            radio.connect("toggled", cb, text)
        radios.append(radio)
        box.add(radio)
    table.attach(box, col+1, col+2, row, row+1)
    return radios

def table_add_separator(table, row):
    "table_add_separator(table,row)"
    widget = Gtk.HSeparator()
    endcol = table.get_property("n-columns")
    # separator for whole table width
    table.attach(widget, 0, endcol, row, row+1, Gtk.AttachOptions.FILL)


# -----------------------------
# File selection helpers

def get_open_filename(title, parent, path = None):
    buttons = (Gtk.STOCK_OK, Gtk.ResponseType.OK, Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL)
    fsel = Gtk.FileChooserDialog(title, parent, Gtk.FileChooserAction.OPEN, buttons)
    fsel.set_local_only(True)
    if path:
        fsel.set_filename(path)
    if fsel.run() == Gtk.ResponseType.OK:
        filename = fsel.get_filename()
    else:
        filename = None
    fsel.destroy()
    return filename

def get_save_filename(title, parent, path = None):
    buttons = (Gtk.STOCK_OK, Gtk.ResponseType.OK, Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL)
    fsel = Gtk.FileChooserDialog(title, parent, Gtk.FileChooserAction.SAVE, buttons)
    fsel.set_local_only(True)
    fsel.set_do_overwrite_confirmation(True)
    if path:
        fsel.set_filename(path)
        if not os.path.exists(path):
            # above set only folder, this is needed to set
            # the file name when the file doesn't exist
            fsel.set_current_name(os.path.basename(path))
    if fsel.run() == Gtk.ResponseType.OK:
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
        fsel = Gtk.FileChooserButton(title=label)
        # Hatari cannot access URIs
        fsel.set_local_only(True)
        fsel.set_width_chars(12)
        fsel.set_action(action)
        if filename:
            fsel.set_filename(filename)
        elif path:
            fsel.set_current_folder(path)
        eject = create_button("Eject", self._eject, fsel)

        box = Gtk.HBox()
        box.pack_start(fsel, True, True, 0)
        box.pack_start(eject, False, False, 0)
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
    def __init__(self, parent, title = "Select a file", validate = None, data = None):
        self._title = title
        self._parent = parent
        self._validate = validate
        self._validate_data = data
        entry = Gtk.Entry()
        entry.set_width_chars(12)
        entry.set_editable(False)
        hbox = Gtk.HBox()
        hbox.add(entry)
        button = create_button("Select...", self._select_file_cb)
        hbox.pack_start(button, False, False, 0)
        self._entry = entry
        self._hbox = hbox

    def _select_file_cb(self, widget):
        fname = self._entry.get_text()
        while True:
            fname = get_save_filename(self._title, self._parent, fname)
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
