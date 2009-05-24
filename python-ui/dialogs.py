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
     create_button, FselEntry


# -----------------
# Dialog base class

class HatariUIDialog:
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
        "run(config) -> False if canceled, True otherwise or if no changes"
        changes = []
        for key, value in config.get_changes():
            changes.append("%s = %s" % (key, str(value)))
        if not changes:
            return True
        child = self.viewport.get_child()
        child.set_text(config.get_path() + ":\n" + "\n".join(changes))
        width, height = child.get_size_request()
        if height < 320:
            self.scrolledwindow.set_size_request(width, height)
        else:
            self.scrolledwindow.set_size_request(-1, 320)
        self.viewport.show_all()
        
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_CANCEL:
            return False
        if response == gtk.RESPONSE_YES:
            config.save()
        return True


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
        "run(hatari) -> True if Hatari killed, False if left running"
        if not hatari.is_running():
            return True
        # Hatari is running, OK to kill?
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_OK:
            hatari.kill()
            return True
        return False

    
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


# ----------------------------------
# Floppy image dialog

class DiskDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Floppy images", 9)
        
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

        table.show_all()

    def _eject(self, widget, fsel):
        fsel.unselect_all()
    
    def run(self, config):
        "run(config), show disk image dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        
        if response == gtk.RESPONSE_APPLY:
            config.lock_updates()
            for drive in range(2):
                config.set_floppy(drive, self.floppy[drive].get_filename())
            config.flush_updates()

    
# ---------------------------
# Display dialog

class DisplayDialog(HatariUIDialog):

    def _create_dialog(self, config):
        tips = gtk.Tooltips()

        skip = gtk.combo_box_new_text()
        for text in config.get_frameskip_names():
            skip.append_text(text)
        skip.set_active(config.get_frameskip())
        tips.set_tip(skip, "Set how many frames are skipped")

        zoom = gtk.CheckButton("Zoom ST-low")
        zoom.set_active(config.get_zoom())
        tips.set_tip(zoom, "Whether to double ST-low resolution")

        borders = gtk.CheckButton("Borders")
        borders.set_active(config.get_borders())
        tips.set_tip(borders, "Whether to show overscan borders in low/mid-rez")

        spec512 = gtk.CheckButton("Spec512")
        spec512.set_active(config.get_spec512threshold())
        tips.set_tip(spec512, "Whether to support Spec512 (>16 colors at the same time)")

        statusbar = gtk.CheckButton("Statusbar")
        statusbar.set_active(config.get_statusbar())
        tips.set_tip(statusbar, "Whether to show statusbar with floppy leds etc")

        led = gtk.CheckButton("Overlay led")
        led.set_active(config.get_led())
        tips.set_tip(led, "Whether to show overlay drive led when statusbar isn't visible")

        dialog = gtk.Dialog("Display settings", self.parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))

        dialog.vbox.add(gtk.Label("Frameskip:"))
        dialog.vbox.add(skip)
        dialog.vbox.add(zoom)
        dialog.vbox.add(borders)
        dialog.vbox.add(spec512)
        dialog.vbox.add(statusbar)
        dialog.vbox.add(led)
        dialog.vbox.show_all()

        self.dialog = dialog
        self.skip = skip
        self.zoom = zoom
        self.borders = borders
        self.spec512 = spec512
        self.statusbar = statusbar
        self.led = led
 
    def run(self, config):
        "run(config), show display dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_APPLY:
            config.lock_updates()
            config.set_frameskip(self.skip.get_active())
            config.set_zoom(self.zoom.get_active())
            config.set_borders(self.borders.get_active())
            config.set_spec512threshold(self.spec512.get_active())
            config.set_statusbar(self.statusbar.get_active())
            config.set_led(self.led.get_active())
            config.flush_updates()


# ----------------------------------
# Joystick dialog

class JoystickDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Joystick settings", 9)
        
        joy = 0
        self.joy = []
        joytypes = config.get_joystick_types()
        for label in config.get_joystick_names():
            combo = gtk.combo_box_new_text()
            for text in joytypes:
                combo.append_text(text)
            combo.set_active(config.get_joystick(joy))
            widget = table_add_widget_row(table, joy, "%s:" % label, combo)
            self.joy.append(widget)
            joy += 1

        table.show_all()
    
    def run(self, config):
        "run(config), show joystick dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        
        if response == gtk.RESPONSE_APPLY:
            config.lock_updates()
            for joy in range(6):
                config.set_joystick(joy, self.joy[joy].get_active())
            config.flush_updates()


# ---------------------------------------
# Peripherals (midi,printer,rs232) dialog

class PeripheralDialog(HatariUIDialog):
    def _create_dialog(self, config):
        midi = gtk.CheckButton("Enable midi output")
        midi.set_active(config.get_midi())

        printer = gtk.CheckButton("Enable printer output")
        printer.set_active(config.get_printer())

        rs232 = gtk.CheckButton("Enable RS232")
        rs232.set_active(config.get_rs232())

        dialog = gtk.Dialog("Peripherals", self.parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))
        dialog.vbox.add(midi)
        dialog.vbox.add(printer)
        dialog.vbox.add(rs232)
        dialog.vbox.show_all()

        self.dialog = dialog
        self.printer = printer
        self.rs232 = rs232
        self.midi = midi
    
    def run(self, config):
        "run(config), show peripherals dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        
        if response == gtk.RESPONSE_APPLY:
            config.lock_updates()
            config.set_midi(self.midi.get_active())
            config.set_printer(self.printer.get_active())
            config.set_rs232(self.rs232.get_active())
            config.flush_updates()


# ---------------------------------------
# Path dialog

class PathDialog(HatariUIDialog):
    def _create_dialog(self, config):
        paths = config.get_paths()
        table, self.dialog = create_table_dialog(self.parent, "File path settings", len(paths))
        paths.sort()
        row = 0
        self.paths = []
        for (key, path, label) in paths:
            fsel = FselEntry(self.dialog, self._validate_fname, key)
            fsel.set_filename(path)
            self.paths.append((key, fsel))
            table_add_widget_row(table, row, label, fsel.get_container())
            row += 1
        table.show_all()
    
    def _validate_fname(self, key, fname):
        if key != "soundout":
            return True
        if fname.rsplit(".", 1)[-1].lower() in ("ym", "wav"):
            return True
        ErrorDialog(self.dialog).run("Sound output file name:\n\t%s\nneeds to end with '.ym' or '.wav'." % fname)
        return False
    
    def run(self, config):
        "run(config), show paths dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()

        if response == gtk.RESPONSE_APPLY:
            paths = []
            for key, fsel in self.paths:
                paths.append((key, fsel.get_filename()))
            config.set_paths(paths)


# ---------------------------
# Sound dialog

class SoundDialog(HatariUIDialog):

    def _create_dialog(self, config):
        combo = gtk.combo_box_new_text()
        for text in config.get_sound_values():
            combo.append_text(text)
        combo.set_active(config.get_sound())
        box = gtk.HBox()
        box.pack_start(gtk.Label("Sound:"), False, False)
        box.add(combo)
        self.sound = combo

        dialog = gtk.Dialog("Sound settings", self.parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL,  gtk.RESPONSE_CANCEL))
        dialog.vbox.add(box)
        dialog.vbox.show_all()
        self.dialog = dialog

    def run(self, config):
        "run(config), show sound dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_APPLY:
            config.set_sound(self.sound.get_active())
        

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
        "ikbd_cmds",
        "ikbd_acia",
        "ikbd_exec",
        "blitter",
        "bios",
        "xbios",
        "gemdos",
        "vdi",
        "io_read",
        "io_write",
        "dmasound"
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


# ------------------------------------------
# Machine dialog for settings needing reboot

class MachineDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Machine configuration", 9, "Set and reboot")

        row = 0
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
        for text in config.get_memory_names():
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
    
    def _get_config(self, config):
        self.machine.set_active(config.get_machine())
        self.monitor.set_active(config.get_monitor())
        self.memory.set_active(config.get_memory())
        tos = config.get_tos()
        hd = config.get_harddisk()
        usehd = config.get_use_harddisk()
        if tos:
            self.tos.set_filename(tos)
        if hd:
            self.harddisk.set_filename(hd)
        if usehd:
            self.usehd.set_active(usehd)
        self.compatible.set_active(config.get_compatible())

    def _set_config(self, config):
        config.lock_updates()
        config.set_machine(self.machine.get_active())
        config.set_monitor(self.monitor.get_active())
        config.set_memory(self.memory.get_active())
        config.set_tos(self.tos.get_filename())
        # usehd has to be before before harddisk
        config.set_use_harddisk(self.usehd.get_active())
        config.set_harddisk(self.harddisk.get_filename())
        config.set_compatible(self.compatible.get_active())
        config.flush_updates()

    def run(self, config):
        "run(config) -> bool, whether to reboot"
        if not self.dialog:
            self._create_dialog(config)

        self._get_config(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == gtk.RESPONSE_APPLY:
            self._set_config(config)
            return True
        return False
