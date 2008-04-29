#!/usr/bin/env python
#
# Classes for Hatari UI dialogs
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


class HatariUIDialog():
    def __init__(self):
        self.dialog = None

    def run(self):
        return self.dialog.run()
    
    def hide(self):
        self.dialog.hide()


class AboutDialog(HatariUIDialog):
    def __init__(self, parent, name, version):
        dialog = gtk.AboutDialog()
        dialog.set_transient_for(parent)
        dialog.set_name(name)
        dialog.set_version(version)
        dialog.set_website("http://hatari.sourceforge.net/")
        dialog.set_website_label("Hatari emulator www-site")
        dialog.set_authors(["Eero Tamminen"])
        dialog.set_artists(["The logo is from Hatari"])
        dialog.set_logo(gtk.gdk.pixbuf_new_from_file("hatari.png"))
        dialog.set_translator_credits("translator-credits")
        dialog.set_copyright("UI copyright (C) 2008 by Eero Tamminen")
        dialog.set_license("""
This software is licenced under GPL v2.

You can see the whole license at:
    http://www.gnu.org/licenses/info/GPLv2.html""")
        self.dialog = dialog


class QuitSaveDialog(HatariUIDialog):
    def __init__(self, parent, config):
        self.config = config
        self.dialog = gtk.MessageDialog(parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            gtk.MESSAGE_QUESTION, gtk.BUTTONS_OK_CANCEL,
            "You have unsaved configuration changes.\n\nQuit anyway?")
    
    def run(self):
        print "Configuration changes:"
        for key, value in self.config.list_changes():
            print "- %s =" % key, value
        response = self.dialog.run()
        print "The configuration that would be saved:"
        self.config.save()
        return response


class KillDialog(HatariUIDialog):
    def __init__(self, parent):
        self.dialog = gtk.MessageDialog(parent,
        gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
        gtk.MESSAGE_QUESTION, gtk.BUTTONS_OK_CANCEL,
        """\
Hatari emulator is already/still running and it needs to be terminated first. However, if it contains unsaved data, that will be lost.

Terminate Hatari anyway?""")


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
    ]
    RESPONSE_CLEAR_ALL = 1  # (builtin Gtk responses are negative)
    
    def __init__(self, parent, hatari):
        dialog = gtk.Dialog("Trace settings", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            ("Clear all", self.RESPONSE_CLEAR_ALL,
             gtk.STOCK_APPLY, gtk.RESPONSE_APPLY))
        dialog.vbox.add(gtk.Label("Select trace points:"))
        self.tracewidgets = {}
        for trace in self.tracepoints:
            widget = gtk.CheckButton(trace)
            self.tracewidgets[trace] = widget
            dialog.vbox.add(widget)
        dialog.vbox.show_all()
        self.dialog = dialog
        self.hatari = hatari
    
    def run(self):
        while True:
            response = self.dialog.run()
            if response == self.RESPONSE_CLEAR_ALL:
                for trace in self.tracepoints:
                    self.tracewidgets[trace].set_active(False)
                continue
            break

        if response == gtk.RESPONSE_APPLY:
            traces = []
            for trace in self.tracepoints:
                if self.tracewidgets[trace].get_active():
                    traces.append(trace)
            if traces:
                self.hatari.change_option("--trace %s" % ",".join(traces))
            else:
                self.hatari.change_option("--trace none")
        return response


class SetupDialog(HatariUIDialog):
    def __init__(self, parent, hatari):
        dialog = gtk.Dialog("Hatari setup", parent,
            gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_APPLY,  gtk.RESPONSE_APPLY,
             gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL))
        notebook = gtk.Notebook()
        self.add_machines(notebook)
        notebook.set_scrollable(True)
        notebook.show_all()
        dialog.vbox.add(notebook)
        self.dialog = dialog
        self.hatari = hatari

    def add_machines(self, notebook):
        for name in ("ST", "STe", "TT", "Falcon"):
            # TODO: TOS (version), amount of memory, disk and HD dir paths
            todo = gtk.Label()
            todo.set_use_markup(True)
            todo.set_markup("<i><b>TODO</b></i>")
            label = gtk.Label(name)
            notebook.append_page(todo, label)
