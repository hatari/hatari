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

import os
# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import pango

from uihelpers import UInfo, HatariTextInsert, create_table_dialog, \
     table_add_entry_row, table_add_widget_row, table_add_separator, \
     create_button


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
# Input dialog

class InputDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = gtk.Dialog("Key/mouse input", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Close", gtk.RESPONSE_CLOSE))
        
        tips = gtk.Tooltips()
        entry = gtk.Entry()
        entry.connect("activate", self._entry_cb)
        insert = create_button("Insert", self._entry_cb)
        tips.set_tip(insert, "Insert text to Hatari window")

        hbox1 = gtk.HBox()
        hbox1.add(gtk.Label("Text:"))
        hbox1.add(entry)
        hbox1.add(insert)
        dialog.vbox.add(hbox1)

        rclick = gtk.Button("Rightclick")
        tips.set_tip(rclick, "Simulate Atari left button double-click")
        rclick.connect("pressed", self._rightpress_cb)
        rclick.connect("released", self._rightrelease_cb)
        dclick = create_button("Doubleclick", self._doubleclick_cb)
        tips.set_tip(dclick, "Simulate Atari rigth button click")

        hbox2 = gtk.HBox()
        hbox2.add(rclick)
        hbox2.add(dclick)
        dialog.vbox.add(hbox2)

        dialog.show_all()
        self.dialog = dialog
        self.entry = entry
    
    def _entry_cb(self, widget):
        text = self.entry.get_text()
        if text:
            HatariTextInsert(self.hatari, text)
            self.entry.set_text("")

    def _doubleclick_cb(self, widget):
        self.hatari.insert_event("doubleclick")

    def _rightpress_cb(self, widget):
        self.hatari.insert_event("rightpress")

    def _rightrelease_cb(self, widget):
        self.hatari.insert_event("rightrelease")

    def run(self, hatari):
        "run(hatari), do text/mouse click input"
        self.hatari = hatari
        self.dialog.run()
        self.dialog.hide()


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
# Reset Hatari dialog

class ResetDialog(HatariUIDialog):
    COLD = 1
    WARM = 2
    def __init__(self, parent):
        self.dialog = gtk.Dialog("Reset Atari?", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Cold reset", self.COLD, "Warm reset", self.WARM,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))
        label = gtk.Label("\nRebooting will lose changes in currently\nrunning Atari programs.\n\nReset anyway?\n")
        self.dialog.vbox.add(label)
        label.show()

    def run(self, hatari):
        "run(hatari) -> True if Hatari rebooted, False if canceled"
        if not hatari.is_running():
            return False
        # Hatari is running, how to reboot?
        response = self.dialog.run()
        self.dialog.hide()
        if response == self.COLD:
            hatari.trigger_shortcut("coldreset")
        elif response == self.WARM:
            hatari.trigger_shortcut("warmreset")
        else:
            return False
        return True

    
# ---------------------------
# Hatari screen dialog

class DisplayDialog(HatariUIDialog):

    def _create_dialog(self, config):
        tips = gtk.Tooltips()

        skip = gtk.HScale()
        skip.set_digits(0)
        skip.set_range(0, 8)
        skip.set_size_request(8*8, -1)
        skip.set_value(config.get_frameskips())
        tips.set_tip(skip, "Increase/decrease screen frame skip")
        
        hbox1 = gtk.HBox()
        hbox1.pack_start(gtk.Label("Frameskip:"), False, False, 0)
        hbox1.add(skip)
        self.skip = skip

        spec512 = gtk.CheckButton("Spec512")
        spec512.set_active(config.get_spec512threshold())
        tips.set_tip(spec512, "Whether to support Spec512 (>16 colors at the same time)")

        borders = gtk.CheckButton("Borders")
        borders.set_active(config.get_borders())
        tips.set_tip(borders, "Whether to show overscan borders in low/mid-rez")

        zoom = gtk.CheckButton("Zoom ST-low")
        zoom.set_active(config.get_zoom())
        tips.set_tip(zoom, "Whether to double ST-low resolution")

        hbox2 = gtk.HBox()
        hbox2.add(spec512)
        hbox2.add(borders)
        hbox2.add(zoom)
        self.zoom = zoom
        self.borders = borders
        self.spec512 = spec512

        dialog = gtk.Dialog("Display settings", self.parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))
        dialog.vbox.add(hbox1)
        dialog.vbox.add(hbox2)
        dialog.vbox.show_all()
        self.dialog = dialog

    def run(self, config):
        "run(config), show display dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_CANCEL:
            return
        config.lock_updates()
        config.set_frameskips(self.skip.get_value())
        config.set_spec512threshold(self.spec512.get_active())
        config.set_borders(self.borders.get_active())
        config.set_zoom(self.zoom.get_active())
        config.flush_updates()
        

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
        self.floppy = []
        path = config.get_floppydir()
        for drive in ("A", "B"):
            label = "Disk %c:" % drive
            fsel = gtk.FileChooserButton(label)
            # Hatari cannot access URIs
            fsel.set_local_only(True)
            fsel.set_width_chars(12)
            filename = config.get_floppy(row)
            if filename:
                fsel.set_filename(filename)
            elif path:
                fsel.set_current_folder(path)
            self.floppy.append(fsel)
            
            eject = create_button("Eject", self._eject, fsel)
            box = gtk.HBox()
            box.pack_start(fsel)
            box.pack_start(eject, False, False)
            table_add_widget_row(table, row, label, box)
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
            widget = table_add_widget_row(table, row + joy, "%s:" % label, combo)
            self.joy.append(widget)
            joy += 1

        # TODO: add printer, serial, midi, RTC to peripherals?
        table.show_all()

    def _eject(self, widget, fsel):
        fsel.unselect_all()
    
    def run(self, config):
        "run() -> file name, file name for given disk"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        
        if response == gtk.RESPONSE_APPLY:
            config.lock_updates()
            for drive in range(2):
                config.set_floppy(drive, self.floppy[drive].get_filename())
            for joy in range(6):
                config.set_joystick(joy, self.joy[joy].get_active())
            config.flush_updates()


# ----------------------------------------
# Setup dialog for settings needing reboot

class SetupDialog(HatariUIDialog):
    def __init__(self, parent):
        self.setups = []
        self.parent = parent
        self.editdialog = None
        self.dialog = None

    def _create_dialog(self, config):
        dialog = gtk.Dialog("Machine configurations", self.parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Set and reboot",  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))
        self.editdialog = EditSetupDialog(dialog)

        box1 = gtk.HBox()
        box1.add(create_button("Add", self._add_setup, config))
        box1.add(create_button("Edit", self._edit_setup, config))
        box1.add(create_button("Remove", self._remove_setup))
        dialog.vbox.add(box1)

        box2 = gtk.HBox()
        self.combo = gtk.combo_box_new_text()
        self.combo.connect("changed", self._show_setup, config)
        box2.pack_start(gtk.Label("Machine setup:"), False, False)
        box2.add(self.combo)
        dialog.vbox.add(box2)

        dialog.vbox.add(gtk.HSeparator())
        self.label = gtk.Label()
        dialog.vbox.add(self.label)

        dialog.vbox.show_all()
        self.dialog = dialog

        self.setups.append({
            "machine": config.get_machine(),
            "monitor": config.get_monitor(),
            "memory": config.get_memory(),
            "tos": config.get_tos(),
            "harddisk": config.get_harddisk(),
            "usehd": config.get_use_harddisk(),
            "compatible": config.get_compatible(),
            "name": "Default"
        })
        self.combo.append_text("Default")
        self.combo.set_active(0)
        
    def _add_setup(self, widget, config):
        current = self.combo.get_active()
        setup = self.setups[current].copy()
        setup["name"] = "NEW"
        setup = self.editdialog.run(config, setup)
        if setup:
            self.setups.append(setup)
            self.combo.append_text(setup["name"])
            self.combo.set_active(current + 1)

    def _edit_setup(self, widget, config):
        current = self.combo.get_active()
        if not current:
            return ErrorDialog(self.parent).run("Cannot edit default setup")
        setup = self.setups[current].copy()
        setup = self.editdialog.run(config, setup)
        if setup:
            self.setups[current] = setup
            self._show_setup(self.combo, config)
            self.combo.remove_text(current)
            self.combo.insert_text(current, setup["name"])
            self.combo.set_active(current)

    def _remove_setup(self, widget):
        current = self.combo.get_active()
        if not current:
            return ErrorDialog(self.parent).run("Cannot delete default setup")
        name = self.setups[current]["name"]
        self.combo.set_active(current - 1)
        self.combo.remove_text(current)
        del(self.setups[current])
        NoteDialog(self.parent).run("Current '%s' setup deleted" % name)

    def _apply_setup(self, config):
        setup = self.setups[self.combo.get_active()]
        config.lock_updates()
        config.set_machine(setup["machine"])
        config.set_monitor(setup["monitor"])
        config.set_memory(setup["memory"])
        config.set_tos(setup["tos"])
        # usehd has to be before before harddisk
        config.set_use_harddisk(setup["usehd"])
        config.set_harddisk(setup["harddisk"])
        config.set_compatible(setup["compatible"])
        config.flush_updates()
    
    def _show_setup(self, combo, config):
        setup = self.setups[combo.get_active()]
        info = []
        info.append("Machine type: %s" % config.get_machine_types()[setup["machine"]])
        info.append("Monitor type: %s" % config.get_monitor_types()[setup["monitor"]])
        info.append("Memory size: %s" % config.get_memory_sizes()[setup["memory"]])
        info.append("TOS image: %s" % os.path.basename(setup["tos"]))
        info.append("Harddisk dir: %s" % setup["harddisk"])
        info.append("Use harddisk: %s" % str(setup["usehd"]))
        info.append("Compatible CPU: %s" % str(setup["compatible"]))
        self.label.set_text("\n".join(info))

    def run(self, config):
        "run() -> bool, whether to reboot"
        if not self.dialog:
            self._create_dialog(config)

        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_APPLY:
            self._apply_setup(config)
            return True
        return False

# ----------------------------------------------
# Dialog for adding/editing setup configurations

class EditSetupDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Add/edit setup", 9)

        row = 0
        self.name = table_add_entry_row(table, row, "Setup name:")
        row += 1
        
        combo = gtk.combo_box_new_text()
        for text in config.get_machine_types():
            combo.append_text(text)
        self.machine = table_add_widget_row(table, row, "Machine type:", combo)
        row += 1
        
        combo = gtk.combo_box_new_text()
        for text in config.get_monitor_types():
            combo.append_text(text)
        self.monitor = table_add_widget_row(table, row, "Monitor type:", combo)
        row += 1
        
        combo = gtk.combo_box_new_text()
        for text in config.get_memory_sizes():
            combo.append_text(text)
        self.memory = table_add_widget_row(table, row, "Memory size:", combo)
        row += 1
        
        label = "TOS image:"
        fsel = self._fsel(label, gtk.FILE_CHOOSER_ACTION_OPEN)
        self.tos = table_add_widget_row(table, row, label, fsel)
        row += 1
        
        label = "Harddisk:"
        fsel = self._fsel(label, gtk.FILE_CHOOSER_ACTION_SELECT_FOLDER)
        self.harddisk = table_add_widget_row(table, row, label, fsel)       
        row += 1

        widget = gtk.CheckButton("Use harddisk")
        self.usehd = table_add_widget_row(table, row, None, widget)
        row += 1

        widget = gtk.CheckButton("Compatible CPU")
        self.compatible = table_add_widget_row(table, row, None, widget)
        row += 1

        table.show_all()

    def _fsel(self, label, action):
        fsel = gtk.FileChooserButton(label)
        # Hatari cannot access URIs
        fsel.set_local_only(True)
        fsel.set_width_chars(12)
        fsel.set_action(action)
        return fsel

    def run(self, config, setup):
        if not self.dialog:
            self._create_dialog(config)

        self.name.set_text(setup["name"])
        self.machine.set_active(setup["machine"])
        self.monitor.set_active(setup["monitor"])
        self.memory.set_active(setup["memory"])
        if setup["tos"]:
            self.tos.set_filename(setup["tos"])
        if setup["harddisk"]:
            self.harddisk.set_filename(setup["harddisk"])
        if setup["usehd"]:
            self.usehd.set_active(setup["usehd"])
        self.compatible.set_active(setup["compatible"])

        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_CANCEL:
            return None
        
        setup["name"] = self.name.get_text()
        setup["machine"] = self.machine.get_active()
        setup["monitor"] = self.monitor.get_active()
        setup["memory"] = self.memory.get_active()
        setup["tos"] = self.tos.get_filename()
        setup["harddisk"] = self.harddisk.get_filename()
        setup["usehd"] = self.usehd.get_active()
        setup["compatible"] = self.compatible.get_active()
        return setup
