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
from dialogs import HatariUIDialog, TodoDialog, ErrorDialog


# base class
class TableDialog(HatariUIDialog):
    def add_entry_row(self, table, row, text, size = None):
        # adds given label right aligned to given row in given table
        # returns entry for that line
        label = gtk.Label(text)
        align = gtk.Alignment(1)
        align.add(label)
        if size:
            entry = gtk.Entry(size)
            entry.set_width_chars(size)
        else:
            entry = gtk.Entry()
        table.attach(align, 0, 1, row, row+1)
        table.attach(entry, 1, 2, row, row+1)
        return entry


class OptionsDialog(TableDialog):
    def __init__(self, parent):
        table = gtk.Table(1, 2) # rows, cols
        table.set_col_spacings(8)
        
        self.lines = self.add_entry_row(table, 0, "Memdump/disasm lines:", 2)
        self.lines.connect("activate", self.entry_cb)
        
        self.dialog = gtk.Dialog("Debug UI Options", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))
        self.dialog.vbox.add(table)
    
    def entry_cb(self, widget):
        self.dialog.response(gtk.RESPONSE_APPLY)
    
    def run(self, lines):
        # show given lines value and that or new one, if new is given
        self.lines.set_text(str(lines))
        self.dialog.show_all()
        while 1:
            response = self.dialog.run()
            if response == gtk.RESPONSE_APPLY:
                text = self.lines.get_text()
                if text:
                    try:
                        lines = int(text)
                        break
                    except ValueError:
                        ErrorDialog(self.dialog).run("lines needs an integer number")
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
        self.dumpmode = self._REGISTERS
        self.clear_addresses()
        
        self.dbg_out_file = self.hatari.open_debug_output()
        self.window = self.create_ui("Hatari Debug UI", icon, do_destroy)
        self.options = OptionsDialog(self.window)
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
        TodoDialog(self.window).run("load data in given file to memory.")

    def memsave_cb(self, widget):
        TodoDialog(self.window).run("save given range of memory to file.")
        
    def options_cb(self, widget):
        lines = self.config.variables.nLines
        lines = self.options.run(lines)
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
        return True


if __name__ == "__main__":
    from hatari import Hatari
    hatari = Hatari()
    hatari.run()
    debugui = HatariDebugUI(hatari, "hatari-icon.png", True)
    debugui.window.show_all()
    gtk.main()
    debugui.save_options()
