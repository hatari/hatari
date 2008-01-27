#!/usr/bin/env python
#
# A PyGtk UI launcher that embeds the Hatari emulator window.
#
# In the future it will provide also some run-time controls
# and a configuration UI for Hatari.  Run-time controls will
# need modifications in Hatari.
#
# Requires python-glade2 package and its dependencies to be present.
#
#
# Copyright (C) 2008 by Eero Tamminen
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
import sys
import gtk
import gtk.glade

class HatariUI():

    def __init__(self):
        self.hatari_pid = 0  # running Hatari emulator PID?
        self.changes = False # unsaved configuration changes?
        self.load_hatari_config()
        self.load_ui()
        
    def run(self):
        self.window.show_all()
        gtk.main()
        
    def load_ui(self):
        handlers = {
            "on_aboutbutton_clicked": self.aboutbutton_clicked,
            "on_runbutton_clicked": self.runbutton_clicked,
            "on_mainwin_destroy": self.mainwin_destroy
        }
        # just instantiate all UI windows/widgets...
        self.wtree = gtk.glade.XML("hatari-ui.glade")
        self.wtree.signal_autoconnect(handlers)
        self.window = self.wtree.get_widget("mainwin")
        self.aboutdialog = self.wtree.get_widget("aboutdialog")
        self.killdialog = self.wtree.get_widget("killdialog")
        self.quitdialog = self.wtree.get_widget("quitdialog")
        # modal dialogs need to be transient to their parents
        self.aboutdialog.set_transient_for(self.window)
        self.killdialog.set_transient_for(self.window)
        self.quitdialog.set_transient_for(self.window)

    def aboutbutton_clicked(self, widget):
        self.aboutdialog.run()
        self.aboutdialog.hide()

    def mainwin_destroy(self, widget):
        if self.ask_hatari_running():
            return
        if self.changes:
            if self.quitdialog.run() == gtk.RESPONSE_OK:
                gtk.main_quit()
            self.quitdialog.hide()

    def load_hatari_config(self):
        print "TODO: load Hatari configuration file"
        # TODO: remove this once testing is done
        self.changes = True

    def ask_hatari_running(self):
        if not self.hatari_pid:
            return False
        response = self.killdialog.run()
        self.killdialog.hide()
        if response == gtk.RESPONSE_OK:
            os.kill(self.hatari_pid, 9)
            print "killed hatari with PID %d" % self.hatari_pid
            self.hatari_pid = 0
            return False
        return True

    def runbutton_clicked(self, widget):
        if self.ask_hatari_running():
            return
        pid = os.fork()
        if pid:
            # in parent
            self.hatari_pid = pid
            print "TODO: add waitpid stuff to notice if Hatari exits"
        else:
            # child runs Hatari
            os.execvpe("hatari", self.get_hatari_args(), self.get_hatari_env())

    def get_hatari_env(self):
        # tell SDL to embed itself inside this widget
        widget = self.wtree.get_widget("hatarieventbox")
        if sys.platform == 'win32':
            win_id = widget.window.handle
        else:
            win_id = widget.window.xid
        env = os.environ
        env["SDL_WINDOWID"] = str(win_id)
        return env
    
    def get_hatari_args(self):
        print "TODO: get the Hatari options from configuration"
        args = ("-m", "-z", "2")
        return args
        
app = HatariUI()
app.run()
