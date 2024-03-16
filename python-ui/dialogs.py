#
# Classes for the Hatari UI dialogs
#
# Copyright (C) 2008-2024 by Eero Tamminen
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
from gi.repository import GdkPixbuf
from gi.repository import Pango

from uihelpers import UInfo, HatariTextInsert, create_table_dialog, \
     table_add_entry_row, table_add_widget_row, table_add_separator, \
     table_add_combo_row, table_add_radio_rows, create_button, \
     FselEntry, FselAndEjectFactory


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
    button = Gtk.ButtonsType.OK
    icontype = Gtk.MessageType.INFO
    textpattern = "\n%s"
    def run(self, text):
        "run(text), show message dialog with given text"
        dialog = Gtk.MessageDialog(self.parent,
        Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
        self.icontype, self.button, self.textpattern % text)
        dialog.run()
        dialog.destroy()

class TodoDialog(NoteDialog):
    textpattern = "\nTODO: %s"

class ErrorDialog(NoteDialog):
    button = Gtk.ButtonsType.CLOSE
    icontype = Gtk.MessageType.ERROR
    textpattern = "\nERROR: %s"


class AskDialog(HatariUIDialog):
    def run(self, text):
        "run(text) -> bool, show question dialog and return True if user OKed it"
        dialog = Gtk.MessageDialog(self.parent,
        Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
        Gtk.MessageType.QUESTION, Gtk.ButtonsType.YES_NO, text)
        response = dialog.run()
        dialog.destroy()
        return (response == Gtk.ResponseType.YES)


# ---------------------------
# About dialog

class AboutDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = Gtk.AboutDialog()
        dialog.set_transient_for(parent)
        dialog.set_name(UInfo.name)
        dialog.set_version(UInfo.version)
        dialog.set_website("http://hatari.tuxfamily.org/")
        dialog.set_website_label("Hatari emulator www-site")
        dialog.set_authors(["Eero Tamminen"])
        dialog.set_artists(["The logo is from Hatari"])
        dialog.set_logo(GdkPixbuf.Pixbuf.new_from_file(UInfo.logo))
        dialog.set_translator_credits("translator-credits")
        dialog.set_copyright(UInfo.copyright)
        dialog.set_license("""
This software is licensed under GPL v2 or later.

You can see the whole license at:
    http://www.gnu.org/licenses/info/GPLv2.html""")
        self.dialog = dialog


# ---------------------------
# Input dialog

class InputDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = Gtk.Dialog("Key/mouse input", parent,
            Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            ("Close", Gtk.ResponseType.CLOSE))

        entry = Gtk.Entry()
        entry.connect("activate", self._entry_cb)
        insert = create_button("Insert", self._entry_cb)
        insert.set_tooltip_text("Insert given text to Hatari window")
        enter = create_button("Enter key", self._enter_cb)
        enter.set_tooltip_text("Simulate Enter key press")

        hbox1 = Gtk.HBox()
        hbox1.add(Gtk.Label(label="Text:"))
        hbox1.add(entry)
        hbox1.add(insert)
        hbox1.add(enter)
        dialog.vbox.add(hbox1)

        rclick = Gtk.Button("Right click")
        rclick.connect("pressed", self._rightpress_cb)
        rclick.connect("released", self._rightrelease_cb)
        rclick.set_tooltip_text("Simulate Atari right button press & release")
        dclick = create_button("Double click", self._doubleclick_cb)
        dclick.set_tooltip_text("Simulate Atari left button double-click")

        hbox2 = Gtk.HBox()
        hbox2.add(dclick)
        hbox2.add(rclick)
        dialog.vbox.add(hbox2)

        dialog.show_all()
        self.dialog = dialog
        self.entry = entry

    def _entry_cb(self, widget):
        text = self.entry.get_text()
        if text:
            HatariTextInsert(self.hatari, text)
            self.entry.set_text("")

    def _enter_cb(self, widget):
        self.hatari.insert_event("keypress 28") # Enter key scancode

    def _doubleclick_cb(self, widget):
        self.hatari.insert_event("doubleclick")

    def _rightpress_cb(self, widget):
        self.hatari.insert_event("rightdown")

    def _rightrelease_cb(self, widget):
        self.hatari.insert_event("rightup")

    def run(self, hatari):
        "run(hatari), do text/mouse click input"
        self.hatari = hatari
        self.dialog.run()
        self.dialog.hide()


# ---------------------------
# Quit and Save dialog

class QuitSaveDialog(HatariUIDialog):
    def __init__(self, parent):
        dialog = Gtk.Dialog("Quit and Save?", parent,
            Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            ("Save changes",    Gtk.ResponseType.YES,
             "Ignore changes",  Gtk.ResponseType.NO,
             Gtk.STOCK_CANCEL,  Gtk.ResponseType.CANCEL))
        dialog.vbox.add(Gtk.Label(label="You have unsaved configuration changes:"))
        viewport = Gtk.Viewport()
        viewport.add(Gtk.Label())
        scrolledwindow = Gtk.ScrolledWindow()
        scrolledwindow.set_propagate_natural_width(True)
        scrolledwindow.set_propagate_natural_height(True)
        scrolledwindow.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
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
        if response == Gtk.ResponseType.CANCEL:
            return False
        if response == Gtk.ResponseType.YES:
            config.save()
        return True


# ---------------------------
# Kill Hatari dialog

class KillDialog(HatariUIDialog):
    def __init__(self, parent):
        self.dialog = Gtk.MessageDialog(parent,
        Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
        Gtk.MessageType.QUESTION, Gtk.ButtonsType.OK_CANCEL,
        """\
Hatari emulator is already/still running and it needs to be terminated first. If emulated applications contain unsaved data, that will be lost.

Terminate Hatari anyway?""")

    def run(self, hatari):
        "run(hatari) -> True if Hatari killed, False if left running"
        if not hatari.is_running():
            return True
        # Hatari is running, OK to kill?
        response = self.dialog.run()
        self.dialog.hide()
        if response == Gtk.ResponseType.OK:
            hatari.kill()
            return True
        return False


# ---------------------------
# Reset Hatari dialog

class ResetDialog(HatariUIDialog):
    COLD = 1
    WARM = 2
    def __init__(self, parent):
        self.dialog = Gtk.Dialog("Reset Atari?", parent,
            Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            ("Cold reset", self.COLD, "Warm reset", self.WARM,
             Gtk.STOCK_CANCEL,  Gtk.ResponseType.CANCEL))
        label = Gtk.Label(label="\nRebooting will lose changes in currently\nrunning Atari programs.\n\nReset anyway?\n")
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

class FloppyDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Floppy images", 4, 2)
        factory = FselAndEjectFactory()

        row = 0
        self.floppy = []
        path = config.get_floppydir()
        for drive in ("A", "B"):
            label = "Disk %c image:" % drive
            fname = config.get_floppy(row)
            fsel, box = factory.get(label, path, fname, Gtk.FileChooserAction.OPEN)
            table_add_widget_row(table, row, 0, label, box)
            self.floppy.append(fsel)
            row += 1

        protect = table_add_combo_row(table, row, 0, "Write protection:", config.get_protection_types())
        protect.set_tooltip_text("Write protect floppy image contents")
        protect.set_active(config.get_floppy_protection())
        row += 1

        vbox = Gtk.VBox()
        ds = Gtk.CheckButton("Double sided drives")
        ds.set_tooltip_text("Whether drives are double or single sided. Can affect behavior of some games")
        ds.set_active(config.get_doublesided())
        vbox.add(ds)

        driveb = Gtk.CheckButton("Drive B connected")
        driveb.set_tooltip_text("Whether drive B is connected. Can affect behavior of some demos & games")
        driveb.set_active(config.get_floppy_drives()[1])
        vbox.add(driveb)

        fastfdc = Gtk.CheckButton("Fast floppy access")
        fastfdc.set_tooltip_text("Can cause incompatibilities with some games/demos")
        fastfdc.set_active(config.get_fastfdc())
        vbox.add(fastfdc)

        table_add_widget_row(table, row, 0, None, vbox)
        row += 1

        table.show_all()

        self.protect = protect
        self.fastfdc = fastfdc
        self.driveb = driveb
        self.ds = ds

    def run(self, config):
        "run(config), show disk image dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()

        if response == Gtk.ResponseType.APPLY:
            config.lock_updates()
            for drive in range(2):
                config.set_floppy(drive, self.floppy[drive].get_filename())
            config.set_floppy_protection(self.protect.get_active())
            config.set_doublesided(self.ds.get_active())
            config.set_fastfdc(self.fastfdc.get_active())
            drives = (config.get_floppy_drives()[0], self.driveb.get_active())
            config.set_floppy_drives(drives)
            config.flush_updates()


# ----------------------------------
# Hard disk dialog

class HardDiskDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Hard disks", 4, 2, "Set and reboot")
        factory = FselAndEjectFactory()

        row = 0
        label = "ASCI HD image:"
        path = config.get_acsi_image()
        fsel, box = factory.get(label, None, path, Gtk.FileChooserAction.OPEN)
        table_add_widget_row(table, row, 0, label, box, True)
        self.acsi = fsel
        row += 1

        label = "IDE HD master image:"
        path = config.get_idemaster_image()
        fsel, box = factory.get(label, None, path, Gtk.FileChooserAction.OPEN)
        table_add_widget_row(table, row, 0, label, box, True)
        self.idemaster = fsel
        row += 1

        label = "IDE HD slave image:"
        path = config.get_ideslave_image()
        fsel, box = factory.get(label, None, path, Gtk.FileChooserAction.OPEN)
        table_add_widget_row(table, row, 0, label, box, True)
        self.ideslave = fsel
        row += 1

        table_add_widget_row(table, row, 0, " ", Gtk.HSeparator(), True)
        row += 1

        label = "GEMDOS drive directory:"
        path = config.get_hd_dir()
        fsel, box = factory.get(label, None, path, Gtk.FileChooserAction.SELECT_FOLDER)
        table_add_widget_row(table, row, 0, label, box, True)
        self.hddir = fsel
        row += 1

        hddrive = table_add_combo_row(table, row, 0, "GEMDOS HD drive:", config.get_hd_drives())
        hddrive.set_tooltip_text("Whether GEMDOS HD emulation uses fixed drive letter, or first free drive letter after ASCI & IDE drives (detection unreliable)")
        self.hddrive = hddrive
        row += 1

        protect = table_add_combo_row(table, row, 0, "Write protection:", config.get_protection_types())
        protect.set_tooltip_text("Whether/how to write protect (GEMDOS HD) emulation files, 'auto' means using host files' own properties")
        self.protect = protect
        row += 1

        lower = table_add_combo_row(table, row, 0, "File names:", config.get_hd_cases())
        lower.set_tooltip_text("What to do with names of files created by Atari programs through GEMDOS HD emulation")
        self.lower = lower

        table.show_all()

    def _get_config(self, config):
        path = config.get_acsi_image()
        if path:
            self.acsi.set_filename(path)
        path = config.get_idemaster_image()
        if path:
            self.idemaster.set_filename(path)
        path = config.get_ideslave_image()
        if path:
            self.ideslave.set_filename(path)
        path = config.get_hd_dir()
        if path:
            self.hddir.set_filename(path)
        self.hddrive.set_active(config.get_hd_drive())
        self.protect.set_active(config.get_hd_protection())
        self.lower.set_active(config.get_hd_case())

    def _set_config(self, config):
        config.lock_updates()
        config.set_acsi_image(self.acsi.get_filename())
        config.set_idemaster_image(self.idemaster.get_filename())
        config.set_ideslave_image(self.ideslave.get_filename())
        config.set_hd_dir(self.hddir.get_filename())
        config.set_hd_drive(self.hddrive.get_active())
        config.set_hd_protection(self.protect.get_active())
        config.set_hd_case(self.lower.get_active())
        config.flush_updates()

    def run(self, config):
        "run(config) -> bool, whether to reboot"
        if not self.dialog:
            self._create_dialog(config)

        self._get_config(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == Gtk.ResponseType.APPLY:
            self._set_config(config)
            return True
        return False


# ---------------------------
# Display dialog

class DisplayDialog(HatariUIDialog):

    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Display settings", 9, 2)

        row = 0
        col = 0
        self.skip = table_add_combo_row(table, row, col, "Frameskip:", config.get_frameskip_names())
        self.skip.set_tooltip_text("Set how many frames are skipped to speed up emulation")
        row += 1

        self.slow = table_add_combo_row(table, row, col, "Slowdown:", config.get_slowdown_names())
        self.slow.set_tooltip_text("VBL wait multiplier to slow down emulation. Breaks sound and large enough slowdown causes mouse clicks not to work.")
        row += 1

        topw, toph = config.get_desktop_size()
        maxw = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 320, topw, 8)
        maxh = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 200, toph, 8)
        maxw.set_tooltip_text("Preferred/maximum zoomed width")
        maxh.set_tooltip_text("Preferred/maximum zoomed height")
        maxw.set_digits(0)
        maxh.set_digits(0)

        self.maxw = table_add_widget_row(table, row, col, "Max zoom width:", maxw)
        row += 1
        self.maxh = table_add_widget_row(table, row, col, "Max zoom height:", maxh)
        row += 1

        vbox = Gtk.VBox()
        force_max = Gtk.CheckButton("Force max resolution (Falcon)")
        force_max.set_tooltip_text("Force maximum resolution to help recording videos of demos which do resolution changes")
        self.force_max = force_max
        vbox.add(force_max)

        desktop = Gtk.CheckButton("Keep desktop resolution (scales)")
        desktop.set_tooltip_text("Keep screen resolution in fullscreen. Avoids potential monitor res switch delay & resulting sound skips")
        self.desktop = desktop
        vbox.add(desktop)

        borders = Gtk.CheckButton("Atari screen borders")
        borders.set_tooltip_text("Show overscan borders in ST/STE low/mid-rez and in Falcon color resolutions. Visible border area is affected by max zoom size")
        self.borders = borders
        vbox.add(borders)

        led = Gtk.CheckButton("Show overlay led")
        led.set_tooltip_text("Show overlay drive led when statusbar is not visible")
        self.led = led
        vbox.add(led)

        statusbar = Gtk.CheckButton("Show statusbar")
        statusbar.set_tooltip_text("Show statusbar with TOS and machine info, disk leds etc")
        self.statusbar = statusbar
        vbox.add(statusbar)

        crop = Gtk.CheckButton("Remove statusbar from screen capture")
        crop.set_tooltip_text("Crop statusbar from screenshots and video recordings")
        self.crop = crop
        vbox.add(crop)

        table_add_widget_row(table, row, col, None, vbox)
        table.show_all()


    def _get_config(self, config):
        self.slow.set_active(0)
        self.skip.set_active(config.get_frameskip())
        confw, confh = config.get_max_size()
        self.maxw.set_value(confw)
        self.maxh.set_value(confh)
        self.force_max.set_active(config.get_force_max())
        self.desktop.set_active(config.get_desktop())
        self.borders.set_active(config.get_borders())
        self.led.set_active(config.get_led())
        self.statusbar.set_active(config.get_statusbar())
        self.crop.set_active(config.get_crop())

    def _set_config(self, config):
        config.lock_updates()
        config.set_slowdown(self.slow.get_active())
        config.set_frameskip(self.skip.get_active())
        config.set_max_size(self.maxw.get_value(), self.maxh.get_value())
        config.set_force_max(self.force_max.get_active())
        config.set_desktop(self.desktop.get_active())
        config.set_borders(self.borders.get_active())
        config.set_led(self.led.get_active())
        config.set_statusbar(self.statusbar.get_active())
        config.set_crop(self.crop.get_active())
        config.flush_updates()

    def run(self, config):
        "run(config), show display dialog"
        if not self.dialog:
            self._create_dialog(config)
        self._get_config(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == Gtk.ResponseType.APPLY:
            self._set_config(config)


# ----------------------------------
# Joystick dialog

class JoystickDialog(HatariUIDialog):
    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Joystick settings", 9, 2)

        joy = 0
        self.joy = []
        joytypes = config.get_joystick_types()
        for label in config.get_joystick_names():
            combo = table_add_combo_row(table, joy, 0, "%s:" % label, joytypes)
            combo.set_active(config.get_joystick(joy))
            self.joy.append(combo)
            joy += 1

        table.show_all()

    def run(self, config):
        "run(config), show joystick dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()

        if response == Gtk.ResponseType.APPLY:
            config.lock_updates()
            for joy in range(6):
                config.set_joystick(joy, self.joy[joy].get_active())
            config.flush_updates()


# ---------------------------------------
# Peripherals (midi,printer,rs232) dialog

class PeripheralDialog(HatariUIDialog):
    def _create_dialog(self, config):
        midi = Gtk.CheckButton("Enable MIDI")
        midi.set_active(config.get_midi())

        printer = Gtk.CheckButton("Enable printer output")
        printer.set_active(config.get_printer())

        rs232 = Gtk.CheckButton("Enable RS232/MFP (!Falcon)")
        rs232.set_active(config.get_rs232())

        scca = Gtk.CheckButton("Enable RS232/SCC-A output (MegaSTE/TT/Falcon")
        scca.set_active(config.get_scca())

        scca_lan = Gtk.CheckButton("Enable RS232/SCC-A Lan output (MegaSTE/TT/Falcon")
        scca_lan.set_active(config.get_scca_lan())

        sccb = Gtk.CheckButton("Enable RS232/SCC-B output (MegaSTE/TT/Falcon")
        sccb.set_active(config.get_sccb())

        dialog = Gtk.Dialog("Peripherals", self.parent,
            Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            (Gtk.STOCK_APPLY,  Gtk.ResponseType.APPLY,
             Gtk.STOCK_CANCEL,  Gtk.ResponseType.CANCEL))
        dialog.vbox.add(midi)
        dialog.vbox.add(printer)
        dialog.vbox.add(rs232)
        dialog.vbox.add(scca)
        dialog.vbox.add(scca_lan)
        dialog.vbox.add(sccb)
        dialog.vbox.show_all()

        self.dialog = dialog
        self.printer = printer
        self.rs232 = rs232
        self.scca = scca
        self.scca_lan = scca_lan
        self.sccb = sccb
        self.midi = midi

    def run(self, config):
        "run(config), show peripherals dialog"
        if not self.dialog:
            self._create_dialog(config)
        response = self.dialog.run()
        self.dialog.hide()

        if response == Gtk.ResponseType.APPLY:
            config.lock_updates()
            config.set_midi(self.midi.get_active())
            config.set_printer(self.printer.get_active())
            config.set_rs232(self.rs232.get_active())
            config.set_scca(self.scca.get_active())
            config.set_scca_lan(self.scca_lan.get_active())
            config.set_sccb(self.sccb.get_active())
            config.flush_updates()


# ---------------------------------------
# Path dialog

class PathDialog(HatariUIDialog):
    def _create_dialog(self, config):
        paths = config.get_paths()
        table, self.dialog = create_table_dialog(self.parent, "File path settings", len(paths), 2)
        row = 0
        self.paths = []
        # sort by path label
        paths.sort(key=lambda info: info[2])
        for (key, path, label) in paths:
            label = "%s:" % label
            fsel = FselEntry(self.dialog, title=label, validate=self._validate_fname, data=key)
            fsel.set_filename(path)
            self.paths.append((key, fsel))
            table_add_widget_row(table, row, 0, label, fsel.get_container())
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

        if response == Gtk.ResponseType.APPLY:
            paths = []
            for key, fsel in self.paths:
                paths.append((key, fsel.get_filename()))
            config.set_paths(paths)


# ---------------------------
# Sound dialog

class SoundDialog(HatariUIDialog):

    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Sound settings", 6, 2, "Apply")
        row = 0

        col = 1
        fullspan = True
        self.enabled = table_add_widget_row(table, row, col, None, Gtk.CheckButton("Sound enabled"), fullspan)
        row += 1

        col = 0
        self.hz = table_add_combo_row(table, row, col, "Sound frequency:", config.get_sound_values())
        row += 1

        self.ymmixer = table_add_combo_row(table, row, col, "YM mixing method:", config.get_ymmixer_types())
        self.ymmixer.set_tooltip_text("Which method is used to mix YM voices")
        row += 1

        bufsize = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 10)
        for pos in ((0, "auto"), (10, "min"), (100, "max")):
            bufsize.add_mark(pos[0], Gtk.PositionType.BOTTOM, pos[1])
        bufsize.set_tooltip_text("SDL sound buffer size in ms. 0 = use default value. In some situations, SDL default may cause large (~0.5s) sound delay at lower frequency.  If you have this problem, try with e.g. 20 ms, otherwise keep at 0.")
        bufsize.set_digits(0)
        self.bufsize = table_add_widget_row(table, row, col, "Sound buffer size:", bufsize)
        row += 1

        vbox = Gtk.VBox()
        self.sync = Gtk.CheckButton("Emulation speed synched to sound output")
        self.sync.set_tooltip_text("Constantly adjust emulation screen update rate to match sound output. Can help if you suffer from sound buffer under/overflow.")
        vbox.add(self.sync)

        self.mic = Gtk.CheckButton("Enable (Falcon) microphone")
        vbox.add(self.mic)

        col = 1
        table_add_widget_row(table, row, col, None, vbox, fullspan)
        row += 1

        table.show_all()

    def _get_config(self, config):
        enabled, curhz = config.get_sound()
        self.enabled.set_active(enabled)
        self.hz.set_active(curhz)

        self.sync.set_active(config.get_sync())
        self.mic.set_active(config.get_mic())
        self.ymmixer.set_active(config.get_ymmixer())
        self.bufsize.set_value(config.get_bufsize())

    def _set_config(self, config):
        config.lock_updates()
        enabled = self.enabled.get_active()
        hz = self.hz.get_active()
        config.set_sound(enabled, hz)
        config.set_mic(self.mic.get_active())
        config.set_sync(self.sync.get_active())
        config.set_ymmixer(self.ymmixer.get_active())
        config.set_bufsize(self.bufsize.get_value())
        config.flush_updates()

    def run(self, config):
        "run(config), show sound dialog"
        if not self.dialog:
            self._create_dialog(config)

        self._get_config(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == Gtk.ResponseType.APPLY:
            self._set_config(config)


# ---------------------------
# Trace settings dialog

class TraceDialog(HatariUIDialog):
    # you can get this list with:
    # hatari --trace help 2>&1|awk '/all$/{next} /^  [^-]/ {printf("        \"%s\",\n", $1)}'|sort
    tracepoints = [
        "acia",
        "aes",
        "bios",
        "blitter",
        "cpu_disasm",
        "cpu_exception",
        "cpu_pairing",
        "cpu_regs",
        "cpu_symbols",
        "cpu_video_cycles",
        "crossbar",
        "dmasound",
        "dsp_disasm",
        "dsp_disasm_mem",
        "dsp_disasm_reg",
        "dsp_host_command",
        "dsp_host_interface",
        "dsp_host_ssi",
        "dsp_interrupt",
        "dsp_state",
        "dsp_symbols",
        "fdc",
        "gemdos",
        "ide",
        "ikbd_acia",
        "ikbd_cmds",
        "ikbd_exec",
        "int",
        "io_read",
        "io_write",
        "keymap",
        "mem",
        "mfp_exception",
        "mfp_read",
        "mfp_start",
        "mfp_write",
        "midi",
        "midi_raw",
        "natfeats",
        "nvram",
        "os_base",
        "psg_read",
        "psg_write",
        "scc",
        "scsi_cmd",
        "scsidrv",
        "vdi",
        "videl",
        "video_addr",
        "video_border_h",
        "video_border_v",
        "video_color",
        "video_hbl",
        "video_res",
        "video_ste",
        "video_sync",
        "video_vbl",
        "vme",
        "xbios"
    ]
    def __init__(self, parent):
        self.savedpoints = None
        hbox1 = Gtk.HBox()
        hbox1.add(create_button("Load", self._load_traces))
        hbox1.add(create_button("Clear", self._clear_traces))
        hbox1.add(create_button("Save", self._save_traces))
        hbox2 = Gtk.HBox()
        vboxes = []
        for idx in (0,1,2,3):
            vboxes.append(Gtk.VBox())
            hbox2.add(vboxes[idx])

        count = 0
        per_side = (len(self.tracepoints)+3)//4
        self.tracewidgets = {}
        for trace in self.tracepoints:
            name = trace.replace("_", "-")
            widget = Gtk.CheckButton(name)
            self.tracewidgets[trace] = widget
            vboxes[count//per_side].pack_start(widget, False, True, 0)
            count += 1

        dialog = Gtk.Dialog("Trace settings", parent,
            Gtk.DialogFlags.MODAL | Gtk.DialogFlags.DESTROY_WITH_PARENT,
            (Gtk.STOCK_APPLY,  Gtk.ResponseType.APPLY,
             Gtk.STOCK_CLOSE, Gtk.ResponseType.CLOSE))
        dialog.vbox.add(hbox1)
        dialog.vbox.add(Gtk.Label(label="Select trace points:"))
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
        if not tracepoints:
            return
        for trace in tracepoints.split(","):
            if trace in self.tracewidgets:
                self.tracewidgets[trace].set_active(True)
            else:
                print("ERROR: unknown trace setting '%s'" % trace)

    def _clear_traces(self, widget = None):
        for trace in self.tracepoints:
            self.tracewidgets[trace].set_active(False)

    def _load_traces(self, widget):
        # this does not load traces, just sets them from internal variable
        # that run method gets from caller and sets. It is up to caller
        # whether the saving or loading happens actually to disk
        self._set_traces(self.savedpoints)

    def _save_traces(self, widget):
        self.savedpoints = self._get_traces()

    def run(self, hatari, savedpoints):
        "run(hatari,tracepoints) -> tracepoints, caller saves tracepoints"
        self.savedpoints = savedpoints
        while 1:
            response = self.dialog.run()
            if response == Gtk.ResponseType.APPLY:
                hatari.change_option("--trace %s" % self._get_traces())
            else:
                self.dialog.hide()
                return self.savedpoints


# ------------------------------------------
# Machine dialog for settings needing reboot

class MachineDialog(HatariUIDialog):
    def _machine_cb(self, widget, types):
        machine = types[widget.get_active()].lower()
        if "mega" in machine:
            self.clocks.set_active(1)
            self.cpulevel.set_active(0)
        elif "st" in machine:
            self.clocks.set_active(0)
            self.cpulevel.set_active(0)
        elif machine == "tt":
            self.clocks.set_active(2)
            self.cpulevel.set_active(3)
        elif machine == "falcon":
            self.clocks.set_active(1)
            self.cpulevel.set_active(3)
            self.dsp.set_active(2)

    def _create_dialog(self, config):
        table, self.dialog = create_table_dialog(self.parent, "Machine configuration", 6, 2, "Set and reboot")

        # option for non-combos
        fullspan = True
        col = 0
        row = 0
        types = config.get_machine_types()
        self.machine = table_add_combo_row(table, row, col, "Machine:", types, self._machine_cb, types)
        row += 1

        vbox1 = Gtk.VBox()
        self.blitter = Gtk.CheckButton("Blitter (ST only)")
        self.blitter.set_tooltip_text("Whether to emulate add-on Blitter chip for ST")
        self.timerd = Gtk.CheckButton("Patch Timer-D")
        self.timerd.set_tooltip_text("Improves ST/STE emulation performance, but some rare demos/games do not work with this")
        vbox1.add(self.blitter)
        vbox1.add(self.timerd)
        table_add_widget_row(table, row, col, "Misc:", vbox1, fullspan)
        row += 1

        self.cpulevel = table_add_combo_row(table, row, col, "CPU type:", config.get_cpulevel_types())
        row += 1

        vbox2 = Gtk.VBox()
        self.compatible = Gtk.CheckButton("Prefetch emulation")
        self.compatible.set_tooltip_text("Needed for overscan and other timing sensitive things to work correctly. Uses more host CPU")
        self.exact = Gtk.CheckButton("Cycle exact with cache emulation")
        self.exact.set_tooltip_text("Cycle exactness increases emulation accuracy and 680x0 cache emulation increases emulated code performance, but requires significantly more host CPU")
        self.mmu = Gtk.CheckButton("MMU emulation")
        vbox2.add(self.compatible)
        vbox2.add(self.exact)
        vbox2.add(self.mmu)
        table_add_widget_row(table, row, col, None, vbox2, fullspan)
        row += 1

        self.clocks = table_add_combo_row(table, row, col, "CPU clock:", config.get_cpuclock_types())
        row += 1

        self.fpu = table_add_combo_row(table, row, col, "FPU type:", config.get_fpu_types())
        row += 1

        self.softfp = Gtk.CheckButton("Accurate FPU emulation")
        self.softfp.set_tooltip_text("Emulate FPU in software instead of using host FPU.  Uses more host CPU")
        table_add_widget_row(table, row, col, None, self.softfp, fullspan)
        row += 1

        self.dsp = table_add_combo_row(table, row, col, "DSP type:", config.get_dsp_types())
        self.dsp.set_tooltip_text("Disable DSP to improve Hatari performance significantly for Falcon programs that work (also) without it.  Some programs using DSP unconditionally, may work with dummy mode (and just lack e.g. sound).")
        row += 1

        self.monitors = table_add_combo_row(table, row, col, "Monitor:", config.get_monitor_types())
        row += 1

        self.memory = table_add_combo_row(table, row, col, "Memory:", config.get_memory_names())
        row += 1

        # use next table column
        col = 2

        ttram = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1024, 4)
        ttram.set_digits(0)
        ttram.set_tooltip_text("TT-RAM requires Falcon/TT and its use disables 24-bit addressing.")
        self.ttram = table_add_widget_row(table, row, col, "TT-RAM:", ttram, fullspan)
        row += 1

        label = "TOS image:"
        fsel = self._fsel(label, Gtk.FileChooserAction.OPEN)
        self.tos = table_add_widget_row(table, row, col, label, fsel, fullspan)
        row += 1

        table.show_all()

    def _fsel(self, label, action):
        fsel = Gtk.FileChooserButton(title=label)
        # Hatari cannot access URIs
        fsel.set_local_only(True)
        fsel.set_width_chars(12)
        fsel.set_action(action)
        return fsel

    def _get_config(self, config):
        self.machine.set_active(config.get_machine())
        self.blitter.set_active(config.get_blitter())
        self.timerd.set_active(config.get_timerd())
        self.cpulevel.set_active(config.get_cpulevel())
        self.compatible.set_active(config.get_compatible())
        self.exact.set_active(config.get_cycle_exact())
        self.mmu.set_active(config.get_mmu())
        self.clocks.set_active(config.get_cpuclock())
        self.fpu.set_active(config.get_fpu_type())
        self.softfp.set_active(config.get_fpu_soft())
        self.dsp.set_active(config.get_dsp())
        self.monitors.set_active(config.get_monitor())
        self.memory.set_active(config.get_memory())
        self.ttram.set_value(config.get_ttram())
        tos = config.get_tos()
        if tos:
            self.tos.set_filename(tos)

    def _get_active_radio(self, radios):
        idx = 0
        for radio in radios:
            if radio.get_active():
                return idx
            idx += 1

    def _set_config(self, config):
        config.lock_updates()
        config.set_machine(self.machine.get_active())
        config.set_blitter(self.blitter.get_active())
        config.set_timerd(self.timerd.get_active())
        config.set_cpulevel(self.cpulevel.get_active())
        config.set_compatible(self.compatible.get_active())
        config.set_cycle_exact(self.exact.get_active())
        config.set_mmu(self.mmu.get_active())
        config.set_cpuclock(self.clocks.get_active())
        config.set_fpu_type(self.fpu.get_active())
        config.set_fpu_soft(self.softfp.get_active())
        config.set_dsp(self.dsp.get_active())
        config.set_monitor(self.monitors.get_active())
        config.set_memory(self.memory.get_active())
        # changes 24/32-bit addressing based on TT-RAM & machine type
        config.set_ttram(self.ttram.get_value(), self.machine.get_active())
        config.set_tos(self.tos.get_filename())
        config.flush_updates()

    def run(self, config):
        "run(config) -> bool, whether to reboot"
        if not self.dialog:
            self._create_dialog(config)

        self._get_config(config)
        response = self.dialog.run()
        self.dialog.hide()
        if response == Gtk.ResponseType.APPLY:
            self._set_config(config)
            return True
        return False
