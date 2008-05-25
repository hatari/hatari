#!/usr/bin/env python
#
# A Debug UI for the Hatari, part of PyGtk Hatari UI
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
# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import pango
import gobject

from config import ConfigStore
from dialogs import HatariUIDialog, NoteDialog, TodoDialog, ErrorDialog, AskDialog


# intermediate class with helper methods
# subclasses need to have:
# - "title" class variable
# - "content_rows" class variable
# - "add_table_content" method
class TableDialog(HatariUIDialog):
    title = None
    content_rows = None
    def add_table_content(self, table):
        pass

    def __init__(self, parent):
        self.dialog = gtk.Dialog(self.title, parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))

        table = gtk.Table(self.content_rows, 2) # rows, cols
        table.set_col_spacings(8)
        self.add_table_content(table)
        self.dialog.vbox.add(table)

    def add_entry_row(self, table, row, text, size = None):
        # adds given label to given row in given table
        # returns entry for that line
        label = gtk.Label(text)
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

    def add_widget_row(self, table, row, text, widget):
        # adds given label right aligned to given row in given table
        # adds given widget to the right column and returns it
        # returns entry for that line
        label = gtk.Label(text)
        align = gtk.Alignment(1)
        align.add(label)
        table.attach(align, 0, 1, row, row+1, gtk.FILL)
        table.attach(widget, 1, 2, row, row+1)
        return widget

    def accept_cb(self, widget):
        self.dialog.response(gtk.RESPONSE_APPLY)


class SaveDialog(TableDialog):
    title = "Save from memory..."
    content_rows = 3

    def add_table_content(self, table):
        entry = gtk.Entry()
        entry.set_width_chars(12)
        entry.set_editable(False)
        self.file = entry
        button = gtk.Button("Select...")
        button.connect("clicked", self.select_file_cb)
        hbox = gtk.HBox()
        hbox.add(entry)
        hbox.pack_start(button, False, False)

        self.add_widget_row(table, 0, "File name:", hbox)
        self.address = self.add_entry_row(table, 1, "Save address:", 6)
        self.address.connect("activate", self.accept_cb)
        self.length = self.add_entry_row(table, 2, "Number of bytes:", 6)
        self.length.connect("activate", self.accept_cb)

    def select_file_cb(self, widget):
        buttons = (gtk.STOCK_OK, gtk.RESPONSE_OK, gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL)
        fsel = gtk.FileChooserDialog("Select save file", self.dialog, gtk.FILE_CHOOSER_ACTION_SAVE, buttons)
        fsel.set_local_only(True)
        if fsel.run() == gtk.RESPONSE_OK:
            self.file.set_text(fsel.get_filename())
        fsel.destroy()
    
    def run(self, address):
        if address:
            self.address.set_text("%06X" % address)
        self.dialog.show_all()
        filename = length = None
        while 1:
            response = self.dialog.run()
            if response == gtk.RESPONSE_APPLY:
                filename = self.file.get_text()
                address_txt = self.address.get_text()
                length_txt = self.length.get_text()
                if filename and address_txt and length_txt:
                    try:
                        address = int(address_txt, 16)
                    except ValueError:
                        ErrorDialog(self.dialog).run("address needs to be in hex")
                        continue
                    try:
                        length = int(length_txt)
                    except ValueError:
                        ErrorDialog(self.dialog).run("length needs to be a number")
                        continue
                    if os.path.exists(filename):
                        question = "File:\n%s\nexists, replace?" % filename
                        if not AskDialog(self.dialog).run(question):
                            continue
                    break
                else:
                    ErrorDialog(self.dialog).run("please fill the field(s)")
            else:
                break
        self.dialog.hide()
        return (filename, address, length)


class LoadDialog(TableDialog):
    title = "Load to memory..."
    content_rows = 2

    def add_table_content(self, table):
        chooser = gtk.FileChooserButton('Select a File')
        chooser.set_local_only(True)  # Hatari cannot access URIs
        chooser.set_width_chars(12)
        self.file = self.add_widget_row(table, 0, "File name:", chooser)
        self.address = self.add_entry_row(table, 1, "Load address:", 6)
        self.address.connect("activate", self.accept_cb)

    def run(self, address):
        if address:
            self.address.set_text("%06X" % address)
        self.dialog.show_all()
        filename = None
        while 1:
            response = self.dialog.run()
            if response == gtk.RESPONSE_APPLY:
                filename = self.file.get_filename()
                address_txt = self.address.get_text()
                if filename and address_txt:
                    try:
                        address = int(address_txt, 16)
                    except ValueError:
                        ErrorDialog(self.dialog).run("address needs to be in hex")
                        continue
                    break
                else:
                    ErrorDialog(self.dialog).run("please fill the field(s)")
            else:
                break
        self.dialog.hide()
        return (filename, address)


class OptionsDialog(TableDialog):
    title = "Debug UI Options"
    content_rows = 1

    def add_table_content(self, table):
        self.lines = self.add_entry_row(table, 0, "Memdump/disasm lines:", 2)
        self.lines.connect("activate", self.accept_cb)
    
    def run(self, lines):
        self.lines.set_text(str(lines))
        self.dialog.show_all()
        while 1:
            Lines = None
            response = self.dialog.run()
            if response == gtk.RESPONSE_APPLY:
                text = self.lines.get_text()
                if text:
                    try:
                        lines = int(text)
                    except ValueError:
                        ErrorDialog(self.dialog).run("lines needs an integer number")
                        continue
                    break
                else:
                    ErrorDialog(self.dialog).run("please fill the field(s)")
            else:
                break
        self.dialog.hide()
        return lines


class HatariDebugUI():
    # dump modes
    _DISASM = 1
    _MEMDUMP = 2
    _REGISTERS = 3
    # move IDs
    _MOVE_MIN = 1
    _MOVE_MED = 2
    _MOVE_MAX = 3
    
    def __init__(self, hatari, icon, do_destroy = False):
        self.hatari = hatari
        
        self.dbg_out_file = self.hatari.open_debug_output()
        self.window = self.create_ui("Hatari Debug UI", icon, do_destroy)
        self.dumpmode = self._REGISTERS
        self.clear_addresses()

        self.dialog_load = None
        self.dialog_save = None
        self.dialog_options = None
        self.load_options()
        
    def clear_addresses(self):
        self.address_first = None
        self.address_second = None
        self.address_last  = None
        
    def create_ui(self, title, icon, do_destroy):
        # buttons at the top
        hbox1 = gtk.HBox()
        self.stop_button = gtk.ToggleButton("Stopped")
        self.stop_button.connect("toggled", self.stop_cb)
        hbox1.add(self.stop_button)

        monitor = gtk.Button("Monitor...")
        monitor.connect("clicked", self.monitor_cb)
        hbox1.add(monitor)
        
        buttons = (
            ("<<<", "Page_Up",  -self._MOVE_MAX),
            ("<<",  "Up",       -self._MOVE_MED),
            ("<",   "Left",     -self._MOVE_MIN),
            (">",   "Right",     self._MOVE_MIN),
            (">>",  "Down",      self._MOVE_MED),
            (">>>", "Page_Down", self._MOVE_MAX)
        )
        self.keys = {}
        for label, keyname, offset in buttons:
            button = gtk.Button(label)
            button.connect("clicked", self.set_address_offset, offset)
            keyval = gtk.gdk.keyval_from_name(keyname)
            self.keys[keyval] =  offset
            hbox1.add(button)

        entry = gtk.Entry(6)
        entry.set_width_chars(6)
        entry.connect("activate", self.address_entry_cb)
        mono = pango.FontDescription("monospace")
        entry.modify_font(mono)
        # to middle of <<>> buttons
        hbox1.pack_start(entry, False)
        hbox1.reorder_child(entry, 5)
        self.address_entry = entry

        # disasm/memory dump at the middle
        self.memory_label = gtk.Label()
        self.memory_label.modify_font(mono)
        align = gtk.Alignment()
        # top, bottom, left, right padding
        align.set_padding(8,0,8,8)
        align.add(self.memory_label)

        # buttons at the bottom
        hbox2 = gtk.HBox()
        radios = (
            ("Registers", self._REGISTERS),
            ("Memdump", self._MEMDUMP),
            ("Disasm", self._DISASM)
        )
        group = None
        for label, mode in radios:
            button = gtk.RadioButton(group, label)
            if not group:
                group = button
            button.connect("toggled", self.dumpmode_cb, mode)
            button.unset_flags(gtk.CAN_FOCUS)
            hbox2.add(button)
        group.set_active(True)

        dialogs = (
            ("Memload...", self.memload_cb),
            ("Memsave...", self.memsave_cb),
            ("Options...", self.options_cb)
        )
        for label, cb in dialogs:
            button = gtk.Button(label)
            button.connect("clicked", cb)
            hbox2.add(button)

        # their containers
        vbox = gtk.VBox()
        vbox.pack_start(hbox1, False)
        vbox.pack_start(align, True, True)
        vbox.pack_start(hbox2, False)
        
        # and the window for all of this
        window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        window.set_events(gtk.gdk.KEY_RELEASE_MASK)
        window.connect("key_release_event", self.key_event_cb)
        if do_destroy:
            window.connect("delete_event", gtk.main_quit)
        else:
            window.connect("delete_event", self.hide)
        window.set_icon_from_file(icon)
        window.set_title(title)
        window.add(vbox)
        return window

    def stop_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
            self.clear_addresses()
            self.dump_address()
        else:
            self.hatari.unpause()

    def dumpmode_cb(self, widget, mode):
        if widget.get_active():
            self.dumpmode = mode
            self.dump_address()

    def address_entry_cb(self, widget):
        try:
            address = int(widget.get_text(), 16)
        except ValueError:
            ErrorDialog(self.window).run("invalid address")
            return
        self.dump_address(address)

    def key_event_cb(self, widget, event):
        if event.keyval in self.keys:
            self.dump_address(None, self.keys[event.keyval])

    def set_address_offset(self, widget, move_idx):
        self.dump_address(None, move_idx)

    def dump_address(self, address = None, move_idx = 0):
        if not address:
            address = self.address_first

        if self.dumpmode == self._REGISTERS:
            self.hatari.debug_command("r")
            output = self.hatari.get_lines(self.dbg_out_file)
            self.memory_label.set_label("".join(output))
            if not self.address_first:
                # second last line has first PC, last line next PC in next column
                self.address_first  = int(output[-2][:output[-2].find(":")], 16)
                self.address_second = int(output[-1][output[-1].find(":")+2:], 16)
                self.address_entry.set_text("%06X" % self.address_first)
            return

        if not address:
            print "ERROR: address needed"
            return
        lines = self.config.variables.nLines

        # on memdump mode move, have no overlap,
        # use one line of overlap in the the disasm mode
        if self.dumpmode == self._MEMDUMP:
            linewidth = 16
            screenful = lines*linewidth
            # no move, left/right, up/down, page up/down
            offsets = [0, 2, linewidth, screenful]
            offset = offsets[abs(move_idx)]
            if move_idx < 0:
                address -= offset
            else:
                address += offset
            address1, address2 = self.address_clamp(address, address+screenful)
            self.hatari.debug_command("m %06x-%06x" % (address1, address2))
            # get & set debugger command results
            output = self.hatari.get_lines(self.dbg_out_file)
            self.memory_label.set_label("".join(output))
            self.address_first = address
            self.address_second = address + linewidth
            self.address_last = address + screenful
            self.address_entry.set_text("%06X" % address)
            return
        
        if self.dumpmode == self._DISASM:
            # TODO: use brute force i.e. ask for more lines that user has
            # requested to be sure that the window is filled, assuming
            # 6 bytes is largest possible instruction+args size
            # (I don't remember anymore my m68k asm...)
            screenful = 6*lines
            # no move, left/right, up/down, page up/down
            offsets = [0, 2, 4, screenful]
            offset = offsets[abs(move_idx)]
            if move_idx < 0:
                address -= offset
                if address < 0:
                    address = 0
                if move_idx == -self._MOVE_MAX and self.address_second:
                    screenful = self.address_second - address
            else:
                if move_idx == self._MOVE_MED and self.address_second:
                    address = self.address_second
                if move_idx == self._MOVE_MAX and self.address_last:
                    address = self.address_last
                else:
                    address += offset
            address1, address2 = self.address_clamp(address, address+screenful)
            self.hatari.debug_command("d %06x-%06x" % (address1, address2))
            # get & set debugger command results
            output = self.hatari.get_lines(self.dbg_out_file)
            # cut output to desired length and check new addresses
            if len(output) > lines:
                if move_idx < 0:
                    output = output[-lines:]
                else:
                    output = output[:lines]
            self.memory_label.set_label("".join(output))
            self.address_first  = int(output[0][:output[0].find(":")], 16)
            self.address_second = int(output[1][:output[1].find(":")], 16)
            self.address_last   = int(output[-1][:output[-1].find(":")], 16)
            self.address_entry.set_text("%06X" % self.address_first)
            return

        print "ERROR: unknown dumpmode:", self.dumpmode
        return

    def address_clamp(self, address1, address2):
        if address1 < 0:
            address2 = address2 - address1
            address1 = 0
        if address2 > 0xffffff:
            address1 = 0xffffff - (address2-address1)
            address2 = 0xffffff
        return address1, address2
    
    def monitor_cb(self, widget):
        TodoDialog(self.window).run("add register / memory address range monitor window.")

    def memload_cb(self, widget):
        if not self.dialog_load:
            self.dialog_load = LoadDialog(self.window)
        (filename, address) = self.dialog_load.run(self.address_first)
        if filename and address:
            self.hatari.debug_command("l %s %06x" % (filename, address))

    def memsave_cb(self, widget):
        if not self.dialog_save:
            self.dialog_save = SaveDialog(self.window)
        (filename, address, length) = self.dialog_save.run(self.address_first)
        if filename and address and length:
            self.hatari.debug_command("s %s %06x %06x" % (filename, address, length))
        
    def options_cb(self, widget):
        if not self.dialog_options:
            self.dialog_options = OptionsDialog(self.window)
        lines = self.config.variables.nLines
        lines = self.dialog_options.run(lines)
        if lines:
            self.config.variables.nLines = lines

    def load_options(self):
        miss_is_error = False # needed for adding windows
        defaults = ({ "[General]": ["nLines"] }, { "nLines": "12" })
        self.config = ConfigStore(defaults, "debugui.cfg", miss_is_error)
    
    def save_options(self):
        self.config.save()
    
    def show(self):
        self.stop_button.set_active(True)
        self.window.show_all()
        self.window.deiconify()

    def hide(self, widget, arg):
        self.window.hide()
        self.stop_button.set_active(False)
        self.save_options()
        return True


if __name__ == "__main__":
    from hatari import Hatari
    hatari = Hatari()
    hatari.run()
    debugui = HatariDebugUI(hatari, "hatari-icon.png", True)
    debugui.window.show_all()
    gtk.main()
    debugui.save_options()
    hatari.kill()
