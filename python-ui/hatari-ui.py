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
import signal

# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gtk.glade


class HatariUI():
    gladefile = "hatari-ui.glade"
    hatari_wd = 640
    hatari_ht = 400

    def __init__(self):
        self.hatari_pid = 0  # running Hatari emulator PID?
        self.changes = False # unsaved configuration changes?
        self.load_hatari_config()
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        # just instantiate all UI windows/widgets...
        mainwin = self.create_mainwin()
        self.create_dialogs(mainwin)
        mainwin.show_all()

    def load_hatari_config(self):
        print "TODO: load Hatari configuration file"
        # TODO: remove this once testing is done
        self.changes = True

    def mainwin_destroy(self, widget):
        if self.ask_hatari_running():
            return
        if self.changes:
            if self.quitdialog.run() == gtk.RESPONSE_OK:
                gtk.main_quit()
            self.quitdialog.hide()
        
    def create_mainwin(self):
        vbox = gtk.VBox()
        button = gtk.Button("About")
        button.unset_flags(gtk.CAN_FOCUS)
        button.connect("clicked", self.about_clicked)
        vbox.add(button)

        button = gtk.Button("Configure")
        button.unset_flags(gtk.CAN_FOCUS)
        button.connect("clicked", self.configure_clicked)
        vbox.add(button)

        button = gtk.ToggleButton("Max Speed")
        button.unset_flags(gtk.CAN_FOCUS)
        button.connect("clicked", self.maxspeed_clicked)
        vbox.add(button)

        button = gtk.Button("Run Hatari!")
        button.unset_flags(gtk.CAN_FOCUS)
        button.connect("clicked", self.run_clicked)
        vbox.add(button)

        button = gtk.Button("Quit")
        button.unset_flags(gtk.CAN_FOCUS)
        button.connect("clicked", self.mainwin_destroy)
        vbox.add(button)
        
        widget = gtk.Socket()
        widget.set_size_request(self.hatari_wd, self.hatari_ht)
        #TODO: self.set_widget_bg_image(widget, "hatari-bg.png")
        widget.set_events(gtk.gdk.ALL_EVENTS_MASK)
        widget.set_flags(gtk.CAN_FOCUS)

        # where to put the Hatari window
        self.hatariparent = widget

        hbox = gtk.HBox()
        hbox.add(widget)
        hbox.add(vbox)
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.connect("destroy", self.mainwin_destroy)
        mainwin.add(hbox)
        return mainwin
    
    def set_widget_bg_image(self, widget, image):
        # works only for widgets with windows of their own
        # such as top level window, eventbox or drawarea
        pixbuf = gtk.gdk.pixbuf_new_from_file(image)
        pixmap, mask = pixbuf.render_pixmap_and_mask()
        del pixbuf
        widget.set_app_paintable(True)
        widget.window.set_back_pixmap(pixmap, False)
        del pixmap
        
    def create_dialogs(self, parent):
        # load UI dialogs from glade file
        wtree = gtk.glade.XML(self.gladefile)
        #TODO: unused wtree.signal_autoconnect(handlers)
        self.aboutdialog = wtree.get_widget("aboutdialog")
        self.killdialog = wtree.get_widget("killdialog")
        self.quitdialog = wtree.get_widget("quitdialog")
        # modal dialogs need to be transient to their parents
        self.aboutdialog.set_transient_for(parent)
        self.killdialog.set_transient_for(parent)
        self.quitdialog.set_transient_for(parent)
    
    def ask_hatari_running(self):
        # is hatari running?
        if not self.hatari_pid:
            return False
        try:
            pid,status = os.waitpid(self.hatari_pid, os.WNOHANG)
        except OSError, value:
            print "Hatari had exited in the meanwhile:\n\t", value
            self.hatari_pid = 0
            return False
        # is running, OK to kill?
        response = self.killdialog.run()
        self.killdialog.hide()
        if response == gtk.RESPONSE_OK:
            os.kill(self.hatari_pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.hatari_pid
            self.hatari_pid = 0
            return False
        return True
        
    def about_clicked(self, widget):
        self.aboutdialog.run()
        self.aboutdialog.hide()
    
    def run_clicked(self, widget):
        if self.ask_hatari_running():
            return
        pid = os.fork()
        if pid < 0:
            print "ERROR: fork()ing Hatari failed!"
            return
        if pid:
            # in parent
            self.hatari_pid = pid
        else:
            # child runs Hatari
            os.execvpe("hatari", self.get_hatari_args(), self.get_hatari_env())

    def get_hatari_env(self):
        window = self.hatariparent.window
        if sys.platform == 'win32':
            win_id = window.handle
        else:
            win_id = window.xid
        env = os.environ
        # tell SDL to use given widget's window
        #env["SDL_WINDOWID"] = str(win_id)

        # above is broken: when SDL uses a window it hasn't created itself,
        # it for some reason doesn't listen to any events delivered to that
        # window nor implements XEMBED protocol to get them in a way most
        # friendly to embedder:
        #   http://standards.freedesktop.org/xembed-spec/latest/
        #
        # Instead we tell hatari to reparent itself after creating
        # its own window into this program widget window
        env["PARENT_WIN_ID"] = str(win_id)
        return env

    def get_hatari_args(self):
        print "TODO: get the Hatari options from configuration"
        args = ("hatari", "-m", "-z", "2")
        return args

    def configure_clicked(self, widget):
        print "TODO: configure dialog"

    def maxspeed_clicked(self, widget):
        print "TODO: maxspeed dialog"


if __name__ == "__main__":
    app = HatariUI()
    gtk.main()
