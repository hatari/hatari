#!/usr/bin/env python
#
# Classes for the Hatari UI dialogs
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
import pango

from uihelpers import UInfo, create_button, create_table_dialog, \
     table_add_entry_row, table_add_widget_row, table_add_separator


# -----------------
# Dialog base class

class HatariUIDialog():
    def __init__(self, parent):
        "<any>Dialog(parent) -> object"
        self.parent = parent
        self.dialog = None
    
    def run(self):
        """run() -> response. Shows dialog and returns response,
subclasses overriding run() require also an argument."""
        response = self.dialog.run()
        self.dialog.hide()
        return response


# ---------------------------
# Note/Todo/Error/Ask dialogs

class NoteDialog(HatariUIDialog):
    icontype = gtk.MESSAGE_INFO
    textpattern = "\n%s"
    def run(self, text):
        "run(text), show message dialog with given text"
        dialog = gtk.MessageDialog(self.parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        self.icontype, gtk.BUTTONS_CLOSE, self.textpattern % text)
        dialog.run()
        dialog.destroy()

class TodoDialog(NoteDialog):
    textpattern = "\nTODO: %s"

class ErrorDialog(NoteDialog):
    icontype = gtk.MESSAGE_ERROR
    textpattern = "\nERROR: %s"


class AskDialog(HatariUIDialog):
    def run(self, text):
        "run(text) -> bool, show question dialog and return True if user OKed it"
        dialog = gtk.MessageDialog(self.parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        gtk.MESSAGE_QUESTION, gtk.BUTTONS_YES_NO, text)
        response = dialog.run()
        dialog.destroy()
        return (response == gtk.RESPONSE_YES)


# ---------------------------
# About dialog

class AboutDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = gtk.AboutDialog()
        dialog.set_transient_for(parent)
        dialog.set_name(UInfo.name)
        dialog.set_version(UInfo.version)
        dialog.set_website("http://hatari.sourceforge.net/")
        dialog.set_website_label("Hatari emulator www-site")
        dialog.set_authors(["Eero Tamminen"])
        dialog.set_artists(["The logo is from Hatari"])
        dialog.set_logo(gtk.gdk.pixbuf_new_from_file(UInfo.logo))
        dialog.set_translator_credits("translator-credits")
        dialog.set_copyright("UI copyright (C) 2008 by Eero Tamminen")
        dialog.set_license("""
This software is licenced under GPL v2.

You can see the whole license at:
    http://www.gnu.org/licenses/info/GPLv2.html""")
        self.dialog = dialog


# ---------------------------
# Paste dialog

class PasteDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = gtk.Dialog("Insert text", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Paste", gtk.RESPONSE_YES))
        entry = gtk.Entry()
        entry.connect("activate", self._entry_cb)
        hbox = gtk.HBox()
        hbox.add(gtk.Label("Text:"))
        hbox.add(entry)
        dialog.vbox.add(hbox)
        dialog.show_all()
        self.dialog = dialog
        self.entry = entry
    
    def _entry_cb(self, widget):
        self.dialog.response(gtk.RESPONSE_YES)
        
    def run(self):
        "run() -> text to insert"
        self.entry.set_text("")
        if self.dialog.run() == gtk.RESPONSE_YES:
            text = self.entry.get_text()
        else:
            text = None
        self.dialog.hide()
        return text


# ---------------------------
# Quit and Save dialog

class QuitSaveDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = gtk.Dialog("Quit and Save?", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Save changes",    gtk.RESPONSE_YES,
             "Discard changes", gtk.RESPONSE_NO,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))
        dialog.vbox.add(gtk.Label("You have unsaved configuration changes:"))
        viewport = gtk.Viewport()
        viewport.add(gtk.Label())
        scrolledwindow = gtk.ScrolledWindow()
        scrolledwindow.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
        scrolledwindow.add(viewport)
        dialog.vbox.add(scrolledwindow)
        dialog.show_all()
        self.scrolledwindow = scrolledwindow
        self.viewport = viewport
        self.dialog = dialog
        
    def run(self, config):
        "run(config) -> RESPONSE_CANCEL if dialog is canceled"
        changes = []
        for key, value in config.get_changes():
            changes.append("%s = %s" % (key, str(value)))
        if not changes:
            return gtk.RESPONSE_NO
        child = self.viewport.get_child()
        child.set_text("\n".join(changes))
        width, height = child.get_size_request()
        if height < 320:
            self.scrolledwindow.set_size_request(width, height)
        else:
            self.scrolledwindow.set_size_request(-1, 320)
        self.viewport.show_all()
        
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_YES:
            config.save()
        return response


# ---------------------------
# Kill Hatari dialog

class KillDialog(HatariUIDialog):
    def __init__(self, parent):
        self.dialog = gtk.MessageDialog(parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        gtk.MESSAGE_QUESTION, gtk.BUTTONS_OK_CANCEL,
        """\
Hatari emulator is already/still running and it needs to be terminated first. However, if it contains unsaved data, that will be lost.

Terminate Hatari anyway?""")

    def run(self, hatari):
        "run(hatari) -> False if Hatari killed, True if left running"
        if not hatari.is_running():
            return False
        # Hatari is running, OK to kill?
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_OK:
            hatari.kill()
            return False
        return True


# ---------------------------
# Trace settings dialog

class TraceDialog(HatariUIDialog):
    # you can get this list with:
    # hatari --trace help 2>&1|awk '/all$/{next} /^  [^-]/ {printf("\"%s\",\n", $1)}'
    tracepoints = [
        "video_sync",
        "video_res",
        "video_color",
        "video_border_v",
        "video_border_h",
        "video_addr",
        "video_hbl",
        "video_vbl",
        "video_ste",
        "mfp_exception",
        "mfp_start",
        "mfp_read",
        "mfp_write",
        "psg_write_reg",
        "psg_write_data",
        "cpu_pairing",
        "cpu_disasm",
        "cpu_exception",
        "int",
        "fdc",
        "ikbd",
        "bios",
        "xbios",
        "gemdos",
        "vdi",
        "blitter"
    ]
    def __init__(self, parent):
        self.savedpoints = "none"
        hbox1 = gtk.HBox()
        hbox1.add(create_button("Load", self._load_traces))
        hbox1.add(create_button("Clear", self._clear_traces))
        hbox1.add(create_button("Save", self._save_traces))
        hbox2 = gtk.HBox()
        vboxes = []
        for idx in (0,1,2):
            vboxes.append(gtk.VBox())
            hbox2.add(vboxes[idx])

        count = 0
        per_side = (len(self.tracepoints)+2)/3
        self.tracewidgets = {}
        for trace in self.tracepoints:
            widget = gtk.CheckButton(trace)
            self.tracewidgets[trace] = widget
            vboxes[count/per_side].pack_start(widget, False, True)
            count += 1
        
        dialog = gtk.Dialog("Trace settings", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE))
        dialog.vbox.add(hbox1)
        dialog.vbox.add(gtk.Label("Select trace points:"))
        dialog.vbox.add(hbox2)
        dialog.vbox.show_all()
        self.dialog = dialog

    def _get_traces(self):
        traces = []
        for trace in self.tracepoints:
            if self.tracewidgets[trace].get_active():
                traces.append(trace)
        if traces:
            return ",".join(traces)
        return "none"

    def _set_traces(self, tracepoints):
        self._clear_traces()
        for trace in tracepoints.split(","):
            if trace in self.tracewidgets:
                self.tracewidgets[trace].set_active(True)
            else:
                print "ERROR: unknown trace setting '%s'" % trace

    def _clear_traces(self, widget = None):
        for trace in self.tracepoints:
            self.tracewidgets[trace].set_active(False)

    def _load_traces(self, widget):
        # this doesn't load traces, just sets them from internal variable
        # that run method gets from caller and sets. It's up to caller
        # whether the saving or loading happens actually to disk
        self._set_traces(self.savedpoints)
    
    def _save_traces(self, widget):
        self.savedpoints = self._get_traces()

    def run(self, hatari, savedpoints):
        "run(hatari,tracepoints) -> tracepoints, caller saves tracepoints"
        self.savedpoints = savedpoints
        while 1:
            response = self.dialog.run()
            if response == gtk.RESPONSE_APPLY:
                hatari.change_option("--trace %s" % self._get_traces())
            else:
                self.dialog.hide()
                return self.savedpoints


# ----------------------------------
# Peripherals (disk/joystick) dialog

class PeripheralsDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Peripheral settings", 9)
        row = 0

        self.file = []
        for drive in ("A", "B"):
            label = "Disk %c:" % drive
            fsel = gtk.FileChooserButton(label)
            # Hatari cannot access URIs
            fsel.set_local_only(True)
            fsel.set_width_chars(12)
            self.file.append(table_add_widget_row(table, row, label, fsel))
            row += 1
        
        table_add_separator(table, row)
        row += 1
        
        joy = 0
        self.joy = []
        joytypes = config.get_joystick_types()
        for label in config.get_joystick_names():
            combo = gtk.combo_box_new_text()
            for text in joytypes:
                combo.append_text(text)
            combo.set_active(config.get_joystick(joy))
            self.joy.append(table_add_widget_row(table, row + joy, label, combo))
            joy += 1

        table.show_all()

    def run(self, config):
        "run() -> file name, file name for given disk"
        if not self.dialog:
            self._create_dialog(config)
        if self.dialog.run() == gtk.RESPONSE_APPLY:
            for drive in range(2):
                config.set_disk(drive, self.file[drive].get_filename())
            for joy in range(6):
                config.set_joystick(joy, self.joy[joy].get_active())
        self.dialog.hide()


# ----------------------------------------
# Setup dialog for settings needing reboot

class SetupDialog(HatariUIDialog):
    def __init__(self, parent):
        self.parent = parent
        self.initialized = False
        dialog = gtk.Dialog("Emulated machine configurations", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Reboot emulation",  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))

        box1 = gtk.HBox()
        box1.add(create_button("Add", self._add_setup))
        box1.add(create_button("Edit", self._edit_setup))
        box1.add(create_button("Remove", self._remove_setup))
        dialog.vbox.add(box1)

        box2 = gtk.HBox()
        self.combo = gtk.combo_box_new_text()
        box2.pack_start(gtk.Label("Machine setup:"), False, False)
        box2.add(self.combo)
        dialog.vbox.add(box2)
        dialog.vbox.show_all()
        self.dialog = dialog

    def _add_setup(self, widget):
        TodoDialog(self.parent).run("Add setup")
    def _edit_setup(self, widget):
        TodoDialog(self.parent).run("Edit setup")
    def _remove_setup(self, widget):
        TodoDialog(self.parent).run("Remove setup")
    
    def _fill_combo(self, config):
        if not self.initialized:
            self.initialized = True
            for text in ("Default", "Another dummy"):
                self.combo.append_text(text)
            self.combo.set_active(0)

    def run(self, config):
        "run() -> bool, whether to reboot"
        self._fill_combo(config)
        if self.dialog.run() == gtk.RESPONSE_APPLY:
            print "TODO: apply setup %d" % self.combo.get_active()
            reboot = True
        else:
            reboot = False
        self.dialog.hide()
        return reboot
