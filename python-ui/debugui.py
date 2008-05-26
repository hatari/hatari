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

from config import ConfigStore
from uihelpers import UInfo, create_button, create_toggle
from dialogs import HatariUIDialog, TodoDialog, ErrorDialog, AskDialog, KillDialog


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
    def __init__(self, parent):
        self.file = None
        self.address = None
        self.length = None
        TableDialog.__init__(self, parent)

    def add_table_content(self, table):
        entry = gtk.Entry()
        entry.set_width_chars(12)
        entry.set_editable(False)
        self.file = entry
        button = create_button("Select...", self.select_file_cb)
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
    def __init__(self, parent):
        self.file = None
        self.address = None
        TableDialog.__init__(self, parent)

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
    def __init__(self, parent):
        self.lines = None
        TableDialog.__init__(self, parent)

    def add_table_content(self, table):
        self.lines = self.add_entry_row(table, 0, "Memdump/disasm lines:", 2)
        self.lines.connect("activate", self.accept_cb)
    
    def run(self, lines):
        self.lines.set_text(str(lines))
        self.dialog.show_all()
        while 1:
            lines = None
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


# ----------------------------------------------------

# constants for the other classes
class Constants:
    # dump modes
    DISASM = 1
    MEMDUMP = 2
    REGISTERS = 3
    # move IDs
    MOVE_MIN = 1
    MOVE_MED = 2
    MOVE_MAX = 3


# class for the memory address entry, view (label) and
# the logic for memory dump modes and moving in memory
class MemoryAddress():
    # class variables
    debug_output = None
    hatari = None

    def __init__(self, hatariobj):
        # hatari
        self.debug_output = hatariobj.open_debug_output()
        self.hatari = hatariobj
        # widgets
        self.entry, self.memory = self.create_widgets()
        # settings
        self.dumpmode = Constants.REGISTERS
        self.lines = 12
        # addresses
        self.first = None
        self.second = None
        self.last = None
        
    def clear(self):
        self.first = None
        self.second = None
        self.last  = None

    def create_widgets(self):
        entry = gtk.Entry(6)
        entry.set_width_chars(6)
        entry.connect("activate", self.entry_cb)
        memory = gtk.Label()
        mono = pango.FontDescription("monospace")
        memory.modify_font(mono)
        entry.modify_font(mono)
        return (entry, memory)

    def entry_cb(self, widget):
        try:
            address = int(widget.get_text(), 16)
        except ValueError:
            ErrorDialog(widget.get_toplevel()).run("invalid address")
            return
        self.dump(address)

    def reset_entry(self):
        self.entry.set_text("%06X" % self.first)
        
    def get(self):
        return self.first

    def get_memory_label(self):
        return self.memory
    
    def get_address_entry(self):
        return self.entry

    def get_lines(self):
        return self.lines
    
    def set_lines(self, lines):
        self.lines = lines
    
    def set_dumpmode(self, mode):
        self.dumpmode = mode
        self.dump()
        
    def dump(self, address = None, move_idx = 0):
        if not address:
            address = self.first
        
        if self.dumpmode == Constants.REGISTERS:
            output = self.get_registers()
            self.memory.set_label("".join(output))
            return
        
        if not address:
            print "ERROR: address needed"
            return
        
        if self.dumpmode == Constants.MEMDUMP:
            output = self.get_memdump(address, move_idx)
        elif self.dumpmode == Constants.DISASM:
            output = self.get_disasm(address, move_idx)
        else:
            print "ERROR: unknown dumpmode:", self.dumpmode
            return
        self.memory.set_label("".join(output))
        if move_idx:
            self.reset_entry()
    
    def get_registers(self):
        self.hatari.debug_command("r")
        output = self.hatari.get_lines(self.debug_output)
        if not self.first:
            # second last line has first PC, last line next PC in next column
            self.first  = int(output[-2][:output[-2].find(":")], 16)
            self.second = int(output[-1][output[-1].find(":")+2:], 16)
            self.reset_entry()
        return output

    def get_memdump(self, address, move_idx):
        linewidth = 16
        screenful = self.lines*linewidth
        # no move, left/right, up/down, page up/down (no overlap)
        offsets = [0, 2, linewidth, screenful]
        offset = offsets[abs(move_idx)]
        if move_idx < 0:
            address -= offset
        else:
            address += offset
        self.set_clamped(address, address+screenful)
        self.hatari.debug_command("m %06x-%06x" % (self.first, self.last))
        # get & set debugger command results
        output = self.hatari.get_lines(self.debug_output)
        self.second = address + linewidth
        return output
        
    def get_disasm(self, address, move_idx):
        # TODO: uses brute force i.e. ask for more lines that user has
        # requested to be sure that the window is filled, assuming
        # 6 bytes is largest possible instruction+args size
        # (I don't remember anymore my m68k asm...)
        screenful = 6*self.lines
        # no move, left/right, up/down, page up/down
        offsets = [0, 2, 4, screenful]
        offset = offsets[abs(move_idx)]
        # force one line of overlap in page up/down
        if move_idx < 0:
            address -= offset
            if address < 0:
                address = 0
            if move_idx == -Constants.MOVE_MAX and self.second:
                screenful = self.second - address
        else:
            if move_idx == Constants.MOVE_MED and self.second:
                address = self.second
            elif move_idx == Constants.MOVE_MAX and self.last:
                address = self.last
            else:
                address += offset
        self.set_clamped(address, address+screenful)
        self.hatari.debug_command("d %06x-%06x" % (self.first, self.last))
        # get & set debugger command results
        output = self.hatari.get_lines(self.debug_output)
        # cut output to desired length and check new addresses
        if len(output) > self.lines:
            if move_idx < 0:
                output = output[-self.lines:]
            else:
                output = output[:self.lines]
        # with disasm need to re-get the addresses from the output
        self.first  = int(output[0][:output[0].find(":")], 16)
        self.second = int(output[1][:output[1].find(":")], 16)
        self.last   = int(output[-1][:output[-1].find(":")], 16)
        return output

    def set_clamped(self, first, last):
        "set_clamped(first,last), clamp addresses to valid address range and set them"
        assert(first < last)
        if first < 0:
            last = last-first
            first = 0
        if last > 0xffffff:
            first = 0xffffff - (last-first)
            last = 0xffffff
        self.first = first
        self.last = last


# the Hatari debugger UI class and methods
class HatariDebugUI():
    
    def __init__(self, hatariobj, do_destroy = False):
        self.address = MemoryAddress(hatariobj)
        self.hatari = hatariobj
        # set when needed/created
        self.dialog_load = None
        self.dialog_save = None
        self.dialog_options = None
        # set when UI created
        self.keys = None
        self.stop_button = None
        # set on option load
        self.config = None
        self.load_options()
        # UI initialization/creation
        self.window = self.create_ui("Hatari Debug UI", do_destroy)
        
    def create_ui(self, title, do_destroy):
        # buttons at top
        hbox1 = gtk.HBox()
        self.create_top_buttons(hbox1)

        # disasm/memory dump at the middle
        align = gtk.Alignment()
        # top, bottom, left, right padding
        align.set_padding(8, 0, 8, 8)
        align.add(self.address.get_memory_label())

        # buttons at bottom
        hbox2 = gtk.HBox()
        self.create_bottom_buttons(hbox2)

        # their container
        vbox = gtk.VBox()
        vbox.pack_start(hbox1, False)
        vbox.pack_start(align, True, True)
        vbox.pack_start(hbox2, False)
        
        # and the window for all of this
        window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        window.set_events(gtk.gdk.KEY_RELEASE_MASK)
        window.connect("key_release_event", self.key_event_cb)
        if do_destroy:
            window.connect("delete_event", self.quit)
        else:
            window.connect("delete_event", self.hide)
        window.set_icon_from_file(UInfo.icon)
        window.set_title(title)
        window.add(vbox)
        return window
    
    def create_top_buttons(self, box):
        self.stop_button = create_toggle("Stop", self.stop_cb)
        box.add(self.stop_button)

        monitor = create_button("Monitor...", self.monitor_cb)
        box.add(monitor)
        
        buttons = (
            ("<<<", "Page_Up",  -Constants.MOVE_MAX),
            ("<<",  "Up",       -Constants.MOVE_MED),
            ("<",   "Left",     -Constants.MOVE_MIN),
            (">",   "Right",     Constants.MOVE_MIN),
            (">>",  "Down",      Constants.MOVE_MED),
            (">>>", "Page_Down", Constants.MOVE_MAX)
        )
        self.keys = {}
        for label, keyname, offset in buttons:
            button = create_button(label, self.set_address_offset, offset)
            keyval = gtk.gdk.keyval_from_name(keyname)
            self.keys[keyval] =  offset
            box.add(button)

        # to middle of <<>> buttons
        address_entry = self.address.get_address_entry()
        box.pack_start(address_entry, False)
        box.reorder_child(address_entry, 5)

    def create_bottom_buttons(self, box):
        radios = (
            ("Registers", Constants.REGISTERS),
            ("Memdump", Constants.MEMDUMP),
            ("Disasm", Constants.DISASM)
        )
        group = None
        for label, mode in radios:
            button = gtk.RadioButton(group, label)
            if not group:
                group = button
            button.connect("toggled", self.dumpmode_cb, mode)
            button.unset_flags(gtk.CAN_FOCUS)
            box.add(button)
        group.set_active(True)

        dialogs = (
            ("Memload...", self.memload_cb),
            ("Memsave...", self.memsave_cb),
            ("Options...", self.options_cb)
        )
        for label, cb in dialogs:
            button = create_button(label, cb)
            box.add(button)

    def stop_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
            self.address.clear()
            self.address.dump()
        else:
            self.hatari.unpause()

    def dumpmode_cb(self, widget, mode):
        if widget.get_active():
            self.address.set_dumpmode(mode)

    def key_event_cb(self, widget, event):
        if event.keyval in self.keys:
            self.address.dump(None, self.keys[event.keyval])

    def set_address_offset(self, widget, move_idx):
        self.address.dump(None, move_idx)
    
    def monitor_cb(self, widget):
        TodoDialog(self.window).run("add register / memory address range monitor window.")

    def memload_cb(self, widget):
        if not self.dialog_load:
            self.dialog_load = LoadDialog(self.window)
        (filename, address) = self.dialog_load.run(self.address.get())
        if filename and address:
            self.hatari.debug_command("l %s %06x" % (filename, address))

    def memsave_cb(self, widget):
        if not self.dialog_save:
            self.dialog_save = SaveDialog(self.window)
        (filename, address, length) = self.dialog_save.run(self.address.get())
        if filename and address and length:
            self.hatari.debug_command("s %s %06x %06x" % (filename, address, length))
        
    def options_cb(self, widget):
        if not self.dialog_options:
            self.dialog_options = OptionsDialog(self.window)
        lines = self.dialog_options.run(self.config.variables.nLines)
        if lines:
            self.config.variables.nLines = lines
            self.address.set_lines(lines)

    def load_options(self):
        # TODO: move config to MemoryAddress class?
        # (depends on how monitoring of addresses should work)
        lines = str(self.address.get_lines())
        miss_is_error = False # needed for adding windows
        defaults = ({ "[General]": ["nLines"] }, { "nLines": lines })
        self.config = ConfigStore(defaults, "debugui.cfg", miss_is_error)
        self.address.set_lines(self.config.variables.nLines)
    
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

    def quit(self, widget, arg):
        KillDialog(self.window).run(self.hatari)
        gtk.main_quit()


def main():
    from hatari import Hatari
    hatariobj = Hatari()
    hatariobj.run()
    debugui = HatariDebugUI(hatariobj, True)
    debugui.window.show_all()
    gtk.main()
    debugui.save_options()

    
if __name__ == "__main__":
    main()
