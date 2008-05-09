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
import time
import select
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
    fifopath = "/tmp/hatari-ui-" + os.getenv("USER") + ".fifo"
    
    def __init__(self, hatari, icon):
        self.hatari = hatari
        self.dumpmode = self._MEMDUMP
        self.address = 0
        
        self.hatari.pause()
        #raw_input("attach strace now, then press Enter\n")
        os.unlink(self.fifopath)
        # TODO: why fifo doesn't work properly (blocks forever on read)
        #os.mkfifo(self.fifopath)
        self.hatari.debug_command("f " + self.fifopath)
        self.window = self.create_ui("Hatari Debug UI", icon)
        time.sleep(0.1)
        self.fifo = open(self.fifopath, "r")
        self.dump_address(self.address)
        self.show()
        
    def create_ui(self, title, icon):
        hbox1 = gtk.HBox()
        stop = gtk.ToggleButton("Stopped")
        stop.set_active(True)
        stop.connect("toggled", self.stop_cb)
        hbox1.add(stop)

        self.address_label =  gtk.Label()
        hbox1.add(self.address_label)
        
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
        if self.dumpmode == self._MEMDUMP:
            memdump.set_active(True)
        else:
            disasm.set_active(True)
        memdump.connect("toggled", self.memdump_cb)
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
        self.memory_label = gtk.Label()
        self.memory_label.set_attributes(pango.parse_markup("<tt></tt>")[0])
        vbox.add(self.memory_label)
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
            self.dump_address(self.address)
        else:
            self.hatari.unpause()

    def dump_address(self, address):
        self.address_label.set_text("0x%06x" % address)
        if self.dumpmode == self._MEMDUMP:
            cmd = "m"
        else:
            cmd = "d"
        # request memory data from Hatari and wait until it's available
        self.hatari.debug_command("%s %06x" % (cmd, address))
        self.memory_label.set_text(self.get_data())
        self.address = address

    def get_data(self):
        # wait until data is available, wait some more for all
        # of it and only then it can be read
        print "Request&wait data from Hatari..."
        select.select([self.fifo], [], [])
        time.sleep(0.1)
        print "...read the data."
        text = "\n" + "".join(self.fifo.readlines())
        print text
        return text

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
        self.dump_address(self.address)

    def registers_cb(self, widget):
        print "TODO: show these in window and allow changing registers"
        self.hatari.debug_command("r")
        self.get_data()

    def monitor_cb(self, widget):
        print "TODO: add monitor window for given memory address range"

    def memload_cb(self, widget):
        print "TODO: load data in given file to memory"

    def memsave_cb(self, widget):
        print "TODO: save given range of memory to file"

    def tracelog_cb(self, widget):
        print "TODO: show tracelog and allow changing trace settings"

    def show(self):
        self.hatari.pause()
        self.window.show_all()
        self.window.deiconify()

    def hide(self, widget, arg):
        # continue Hatari, hide window, don't destroy it
        self.hatari.unpause()
        self.window.hide()
        return True
