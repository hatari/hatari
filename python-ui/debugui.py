#!/usr/bin/env python3
#
# A Debug UI for the Hatari, part of Python Gtk Hatari UI
#
# Copyright (C) 2008-2022 by Eero Tamminen
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
import gi
# use correct version of gtk
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Pango

from config import ConfigStore
from uihelpers import UInfo, create_button, create_toggle, \
     create_table_dialog, table_add_entry_row, table_add_widget_row, \
     get_save_filename, FselEntry
from dialogs import TodoDialog, ErrorDialog, AskDialog, KillDialog


def dialog_apply_cb(widget, dialog):
    dialog.response(Gtk.ResponseType.APPLY)


# -------------
# Table dialogs

class SaveDialog:
    def __init__(self, parent):
        table, self.dialog = create_table_dialog(parent, "Save from memory", 3, 2)
        self.fsel = FselEntry(self.dialog, "Memory save file name:")
        table_add_widget_row(table, 0, 0, "File name:", self.fsel.get_container())
        self.address = table_add_entry_row(table, 1, 0, "Save address:", 6)
        self.address.connect("activate", dialog_apply_cb, self.dialog)
        self.length = table_add_entry_row(table, 2, 0, "Number of bytes:", 6)
        self.length.connect("activate", dialog_apply_cb, self.dialog)

    def run(self, address):
        "run(address) -> (filename,address,length), all as strings"
        if address:
            self.address.set_text("%06X" % address)
        self.dialog.show_all()
        filename = length = None
        while 1:
            response = self.dialog.run()
            if response == Gtk.ResponseType.APPLY:
                filename = self.fsel.get_filename()
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


class LoadDialog:
    def __init__(self, parent):
        chooser = Gtk.FileChooserButton(title="Select memory file to load:")
        chooser.set_local_only(True)  # Hatari cannot access URIs
        chooser.set_width_chars(12)
        table, self.dialog = create_table_dialog(parent, "Load to memory", 2, 2)
        self.fsel = table_add_widget_row(table, 0, 0, "File name:", chooser)
        self.address = table_add_entry_row(table, 1, 0, "Load address:", 6)
        self.address.connect("activate", dialog_apply_cb, self.dialog)

    def run(self, address):
        "run(address) -> (filename,address), all as strings"
        if address:
            self.address.set_text("%06X" % address)
        self.dialog.show_all()
        filename = None
        while 1:
            response = self.dialog.run()
            if response == Gtk.ResponseType.APPLY:
                filename = self.fsel.get_filename()
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


class OptionsDialog:
    def __init__(self, parent):
        self.dialog = Gtk.Dialog("Debugger UI options", parent,
            modal=True, destroy_with_parent=True)
        self.dialog.add_buttons(
            Gtk.STOCK_APPLY,  Gtk.ResponseType.APPLY,
            Gtk.STOCK_CLOSE, Gtk.ResponseType.CLOSE
        )
        lines = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 50, 5)
        lines.set_tooltip_text("Change view mode afterwards to get new (min size) into effect")
        lines.set_digits(0)
        self.lines = lines

        follow = Gtk.CheckButton(label="On stop, set address to PC")
        follow.set_tooltip_text("Run-time option, not saved to Debugger config file")
        self.follow_pc = follow

        vbox = self.dialog.vbox
        vbox.add(Gtk.Label(label="Memdump/disasm lines:"))
        vbox.add(lines)
        vbox.add(follow)
        vbox.show_all()

    def run(self, lines, follow_pc):
        "run(lines,follow_pc) -> (lines,follow_pc)"
        self.follow_pc.set_active(follow_pc)
        self.lines.set_value(lines)
        self.dialog.show_all()
        response = self.dialog.run()
        if response == Gtk.ResponseType.APPLY:
            lines = int(self.lines.get_value())
            follow_pc = self.follow_pc.get_active()
        self.dialog.hide()
        return (lines, follow_pc)


# ----------------------------------------------------

# constants for the other classes
class Constants:
    # dump modes
    NODUMP = 0
    DISASM = 1
    MEMDUMP = 2
    REGISTERS = 3
    # move IDs
    MOVE_MIN = 1
    MOVE_MED = 2
    MOVE_MAX = 3


# class for the memory address entry, view (label) and
# the logic for memory dump modes and moving in memory
class MemoryAddress:
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
        self.dumpmode = Constants.NODUMP
        self.follow_pc = True
        self.lines = 12
        # addresses
        self.first = None
        self.second = None
        self.last = None

    def clear(self):
        if self.follow_pc:
            # get first address from PC when next stopped
            self.first = None
        self.second = None
        self.last  = None

    def create_widgets(self):
        entry = Gtk.Entry(max_length=6, width_chars=7)
        entry.connect("activate", self._entry_cb)
        memory = Gtk.TextView()
        memory.set_monospace(True)
        memory.set_cursor_visible(False)
        memory.set_editable(False)
        return (entry, memory)

    def _entry_cb(self, widget):
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

    def get_memory_view(self):
        return self.memory

    def get_address_entry(self):
        return self.entry

    def get_follow_pc(self):
        return self.follow_pc

    def set_follow_pc(self, follow_pc):
        self.follow_pc = follow_pc

    def get_lines(self):
        return self.lines

    def set_lines(self, lines):
        self.lines = lines

    def set_dumpmode(self, mode):
        if mode == self.dumpmode:
            return
        self.dumpmode = mode
        self.dump()

    def dump(self, address = None, move_idx = 0):
        if self.dumpmode == Constants.REGISTERS:
            output = self._get_registers()
            text = self.memory.get_buffer()
            text.set_text("".join(output))
            return

        if not address:
            if not self.first:
                self._get_registers()
            address = self.first

        if not address:
            print("ERROR: address needed")
            return

        if self.dumpmode == Constants.MEMDUMP:
            output = self._get_memdump(address, move_idx)
        elif self.dumpmode == Constants.DISASM:
            output = self._get_disasm(address, move_idx)
        else:
            print("ERROR: unknown dumpmode:", self.dumpmode)
            return
        text = self.memory.get_buffer()
        text.set_text("".join(output))
        if move_idx:
            self.reset_entry()

    def _get_registers(self):
        self.hatari.debug_command("r")
        output = self.hatari.get_lines(self.debug_output)
        if not self.first:
            # 2nd last line has first PC in 1st column, last line next PC in 2nd column
            self.second = int(output[-1][output[-1].find(":")+2:], 16)
            # OldUAE CPU core has ':' in both
            offset = output[-2].find(":")
            if offset < 0:
                # WinUAE CPU core only in one
                offset = output[-2].find(" ")
            if offset < 0:
                print("ERROR: unable to parse register dump line:\n\t'%s'", output[-2])
                return output
            self.first = int(output[-2][:offset], 16)
            self.reset_entry()
        return output

    def _get_memdump(self, address, move_idx):
        linewidth = 16
        screenful = self.lines*linewidth
        # no move, left/right, up/down, page up/down (no overlap)
        offsets = [0, 2, linewidth, screenful]
        offset = offsets[abs(move_idx)]
        if move_idx < 0:
            address -= offset
        else:
            address += offset
        self._set_clamped(address, address+screenful)
        self.hatari.debug_command("m $%06x-$%06x" % (self.first, self.last))
        # get & set debugger command results
        output = self.hatari.get_lines(self.debug_output)
        self.second = address + linewidth
        return output

    def _get_line_addr(self, line, offset):
        return int(line[offset:line.find(' ')], 16)

    def _get_disasm(self, address, move_idx):
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
        self._set_clamped(address, address+screenful)
        self.hatari.debug_command("d $%06x-$%06x" % (self.first, self.last))
        # get & set debugger command results
        output = self.hatari.get_lines(self.debug_output)
        # cut output to desired length and check new addresses
        if len(output) > self.lines:
            if move_idx < 0:
                output = output[-self.lines:]
            else:
                output = output[:self.lines]
        # remove symbol lines from the output for address checking
        addresses = [line for line in output if ' ' in line]
        # address offset for UAE core and external disassembler output
        offset = 0
        if addresses[0][0] == '$':
            offset = 1
        # need to re-get the addresses from the disassembly output
        self.first  = self._get_line_addr(addresses[0], offset)
        self.second = self._get_line_addr(addresses[1], offset)
        self.last   = self._get_line_addr(addresses[-1], offset)
        return output

    def _set_clamped(self, first, last):
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
class HatariDebugUI:

    def __init__(self, hatariobj, do_destroy = False):
        self.hatari = hatariobj
        self.address = MemoryAddress(hatariobj)
        self.address.set_dumpmode(Constants.REGISTERS)
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
        hbox1 = Gtk.HBox()
        self.create_top_buttons(hbox1)

        # disasm/memory dump at the middle
        addr = self.address.get_memory_view()

        # buttons at bottom
        hbox2 = Gtk.HBox()
        self.create_bottom_buttons(hbox2)

        # their container
        vbox = Gtk.VBox()
        vbox.pack_start(hbox1, False, True, 0)
        vbox.pack_start(addr, True, True, 0)
        vbox.pack_start(hbox2, False, True, 0)

        # and the window for all of this
        window = Gtk.Window(type=Gtk.WindowType.TOPLEVEL)
        window.set_events(Gdk.EventMask.KEY_RELEASE_MASK)
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
            ("<",   None,       -Constants.MOVE_MIN),
            (">",   None,        Constants.MOVE_MIN),
            (">>",  "Down",      Constants.MOVE_MED),
            (">>>", "Page_Down", Constants.MOVE_MAX)
        )
        self.keys = {}
        for label, keyname, offset in buttons:
            button = create_button(label, self.set_address_offset, offset)
            if keyname:
                keyval = Gdk.keyval_from_name(keyname)
                self.keys[keyval] =  offset
            box.add(button)

        # to middle of <<>> buttons
        address_entry = self.address.get_address_entry()
        box.pack_start(address_entry, False, True, 0)
        box.reorder_child(address_entry, 5)

    def create_bottom_buttons(self, box):
        radios = (
            ("Registers", Constants.REGISTERS),
            ("Memdump", Constants.MEMDUMP),
            ("Disasm", Constants.DISASM)
        )
        group = None
        for label, mode in radios:
            button = Gtk.RadioButton(label=label, group=group, can_focus=False)
            if not group:
                group = button
            button.connect("toggled", self.dumpmode_cb, mode)
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
            self.hatari.debug_command("l %s $%06x" % (filename, address))

    def memsave_cb(self, widget):
        if not self.dialog_save:
            self.dialog_save = SaveDialog(self.window)
        (filename, address, length) = self.dialog_save.run(self.address.get())
        if filename and address and length:
            self.hatari.debug_command("s %s $%06x $%06x" % (filename, address, length))

    def options_cb(self, widget):
        if not self.dialog_options:
            self.dialog_options = OptionsDialog(self.window)
        old_lines = self.config.get("[Debugger]", "nDisasmLines")
        old_follow_pc = self.address.get_follow_pc()
        lines, follow_pc = self.dialog_options.run(old_lines, old_follow_pc)
        if lines != old_lines:
            self.config.set("[Debugger]", "nDisasmLines", lines)
            self.address.set_lines(lines)
        if follow_pc != old_follow_pc:
            self.address.set_follow_pc(follow_pc)
        self.address.dump()

    def load_options(self):
        # TODO: move config to MemoryAddress class?
        # (depends on how monitoring of addresses should work)
        lines = self.address.get_lines()
        # ConfigStore does checks and type conversions based on names
        # of Hatari config sections and keys, so this needs to use
        # same names to avoid asserts, and it can't e.g. save
        # follow_pc option value, which will keep as run-time one
        defaults = {
            "[Debugger]": {
                "nDisasmLines": lines,
            }
        }
        userconfdir = ".hatari"
        config = ConfigStore(userconfdir, defaults)
        configpath = config.get_filepath(".debugui.cfg")
        config.load(configpath) # set defaults
        try:
            self.address.set_lines(config.get("[Debugger]", "nDisasmLines"))
        except (KeyError, AttributeError):
            ErrorDialog(None).run("Debug UI configuration mismatch!\nTry again after removing: '%s'." % configpath)
        self.config = config

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
        Gtk.main_quit()


def main():
    import sys
    from hatari import Hatari
    hatariobj = Hatari()
    if len(sys.argv) > 1:
        if sys.argv[1] in ("-h", "--help"):
            print("usage: %s [hatari options]" % os.path.basename(sys.argv[0]))
            return
        args = sys.argv[1:]
    else:
        args = None
    hatariobj.run(args)

    info = UInfo()
    debugui = HatariDebugUI(hatariobj, True)
    debugui.window.show_all()
    Gtk.main()
    debugui.save_options()


if __name__ == "__main__":
    main()
