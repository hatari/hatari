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

# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gobject

class HatariDebugUI():
    # constants
    _DISASM = 1
    _MEMDUMP = 2
    
    def __init__(self, hatari, icon):
        self.hatari = hatari
        self.hatari.change_option("--debug")
        self.window = self.create_ui("Hatari Debug UI", icon)
        self.show()
    
    def create_ui(self, title, icon):
        hbox1 = gtk.HBox()
        stop = gtk.ToggleButton("Stopped")
        stop.connect("toggled", self.stop_cb)
        stop.set_active(True)
        hbox1.add(stop)
        buttons = (
            ("<<", self.pageup_cb),
            ("<",  self.up_cb),
            (">",  self.down_cb),
            (">>", self.pagedown_cb)
        )
        for label, cb in buttons:
            button = gtk.Button(label)
            button.connect("clicked", cb)
            hbox1.add(button)
        
        hbox2 = gtk.HBox()
        memdump = gtk.RadioButton(None, "Memdump")
        disasm = gtk.RadioButton(memdump, "Disasm")
        memdump.connect("toggled", self.memdump_cb)
        memdump.set_active(True)
        hbox2.add(memdump)
        hbox2.add(disasm)

        dialogs = (
            ("Registers",  self.registers_cb),
            ("Monitor...", self.monitor_cb),
            ("Memload...", self.memload_cb),
            ("Memsave...", self.memsave_cb),
            ("Tracelog",   self.tracelog_cb)
        )
        for label, cb in dialogs:
            button = gtk.Button(label)
            button.connect("clicked", cb)
            hbox2.add(button)

        vbox = gtk.VBox()
        vbox.add(hbox1)
        vbox.add(hbox2)
        
        window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        window.connect("delete_event", self.hide)
        window.set_icon_from_file(icon)
        window.set_title(title)
        window.add(vbox)
        return window

    def stop_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
        else:
            self.hatari.unpause()
    
    def pageup_cb(self, widget):
        print "TODO: page back in memory"

    def up_cb(self, widget):
        print "TODO: line back in memory"

    def down_cb(self, widget):
        print "TODO: line down in memory"

    def pagedown_cb(self, widget):
        print "TODO: page down in memory"

    def memdump_cb(self, widget):
        if widget.get_active():
            print "Dumpmode on MEMDUMP"
            self.dumpmode = self._MEMDUMP
        else:
            print "Dumpmode on DISASM"
            self.dumpmode = self._DISASM

    def registers_cb(self, widget):
        print "TODO: show these in window and allow changing registers"
        self.hatari.debug_command("r")

    def monitor_cb(self, widget):
        print "TODO: add monitor window for given memory address range"

    def memload_cb(self, widget):
        print "TODO: load data in given file to memory"

    def memsave_cb(self, widget):
        print "TODO: save given range of memory to file"

    def tracelog_cb(self, widget):
        print "TODO: show tracelog and allow changing trace settings"

    def show(self):
        self.window.show_all()
        self.window.deiconify()
        self.hatari.pause()

    def hide(self, widget, arg):
        # no window destroy
        self.hatari.unpause()
        self.window.hide()
        return True
