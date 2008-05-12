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

class HatariDebugUI():
    # constants
    _DISASM = 1
    _MEMDUMP = 2
    _REGISTERS = 3
    
    def __init__(self, hatari, icon):
        self.hatari = hatari
        self.address = None
        self.default = None
        self.dumpmode = self._DISASM
        self.no_destroy = True
        
        self.window = self.create_ui("Hatari Debug UI", icon)
        self.dbg_out_file = self.hatari.open_debug_output()
        
    def create_ui(self, title, icon):
        # buttons at the top
        hbox1 = gtk.HBox()
        self.stop_button = gtk.ToggleButton("Stopped")
        self.stop_button.connect("toggled", self.stop_cb)
        hbox1.add(self.stop_button)

        monitor = gtk.Button("Monitor...")
        monitor.connect("clicked", self.monitor_cb)
        hbox1.add(monitor)
        
        buttons = (
            ("<<", -64),
            ("<",   -8),
            (">",    8),
            (">>",  64)
        )
        for label, offset in buttons:
            button = gtk.Button(label)
            button.connect("clicked", self.set_address_offset, offset)
            hbox1.add(button)

        default = gtk.Button("Default")
        default.connect("clicked", self.default_cb)
        hbox1.add(default)

        entry =  gtk.Entry(6)
        entry.connect("activate", self.address_entry_cb)
        hbox1.pack_start(entry, False)
        hbox1.reorder_child(entry, 4) # to middle of <<>> buttons
        self.address_entry = entry

        # disasm/memory dump at the middle
        self.memory_label = gtk.Label()
        self.memory_label.modify_font(pango.FontDescription("monospace"))
        align = gtk.Alignment()
        align.set_padding(8,0,8,8) # top, bottom, left, right
        align.add(self.memory_label)

        # buttons at the bottom
        hbox2 = gtk.HBox()
        radios = (
            ("Disasm", self._DISASM),
            ("Memdump", self._MEMDUMP),
            ("Registers", self._REGISTERS)
        )
        group = None
        for label, mode in radios:
            button = gtk.RadioButton(group, label)
            if not group:
                group = button
            button.connect("toggled", self.dumpmode_cb, mode)
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
        window.connect("delete_event", self.hide)
        window.set_icon_from_file(icon)
        window.set_title(title)
        window.add(vbox)
        return window

    def stop_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
            self.address = None
            self.dump_address(self.address)
        else:
            self.hatari.unpause()

    def dumpmode_cb(self, widget, mode):
        if widget.get_active():
            self.dumpmode = mode
            self.dump_address(self.address)

    def address_entry_cb(self, widget):
        try:
            address = int(widget.get_text(), 16)
        except ValueError:
            widget.modify_font(pango.FontDescription("red"))
            widget.set_text("ERROR")
            return
        self.dump_address(address)

    def set_address_offset(self, widget, offset):
        if not self.address:
            print "ERROR: no address"
            return
        self.dump_address(self.address + offset)

    def default_cb(self, widget):
        if self.default:
            self.dump_address(self.default)
        else:
            self.dialog("No default address specified in Options.")

    def dump_address(self, address):
        if self.dumpmode == self._REGISTERS:
            self.hatari.debug_command("r")
            data = self.hatari.get_data(self.dbg_out_file)
            self.memory_label.set_label(data)
            self.set_address(address)
            return
        
        if self.dumpmode == self._DISASM:
            cmd = "d"
        elif self.dumpmode == self._MEMDUMP:
            if not address:
                print "ERROR: memdump mode needs always an address"
                return
            cmd = "m"
        else:
            print "ERROR: unknown dumpmode:", self.dumpmode
            return
        # request memory data from Hatari and wait until it's available
        if address:
            self.hatari.debug_command("%s %06x" % (cmd, address))
        else:
            self.hatari.debug_command(cmd)
        data = self.hatari.get_data(self.dbg_out_file)
        self.memory_label.set_label(data)
         # debugger data begins with a hex address of the dump
        self.set_address(int(data[:data.find(":")], 16))

    def set_address(self, address):
        if address:
            self.address_entry.set_text("%06X" % address)
            self.address = address

    def monitor_cb(self, widget):
        self.dialog("TODO: add register / memory address range monitor window.")

    def memload_cb(self, widget):
        self.dialog("TODO: load data in given file to memory.")

    def memsave_cb(self, widget):
        self.dialog("TODO: save given range of memory to file.")

    def options_cb(self, widget):
        self.dialog("TODO: set step sizes, default address etc.")

    def dialog(self, text):
        dialog = gtk.MessageDialog(self.window,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        gtk.MESSAGE_INFO, gtk.BUTTONS_CLOSE, "\n%s" % text)
        dialog.run()

    def show(self):
        self.stop_button.set_active(True)
        self.window.show_all()
        self.window.deiconify()

    def hide(self, widget, arg):
        self.window.hide()
        self.stop_button.set_active(False)
        if self.no_destroy:
            return True
        else:
            gtk.main_quit()
            return False

    def set_no_destroy(self, no_destroy):
        self.no_destroy = no_destroy


if __name__ == "__main__":
    from hatari import Hatari
    hatari = Hatari()
    hatari.run()
    debugui = HatariDebugUI(hatari, "hatari-icon.png")
    debugui.set_no_destroy(False)
    debugui.window.show_all()
    gtk.main()
