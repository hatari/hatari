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
import sys

# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gtk.glade

from hatari import Hatari, Config

def connect_true(object):
    # disable Socket destroy on Plug (Hatari) disappearance
    return True

class HatariUI():
    title = "Hatari UI v0.2"
    icon = "hatari-icon.png"
    gladefile = "hatari-ui.glade"
    hatari_wd = 640
    hatari_ht = 400

    def __init__(self, fullscreen = False, embed = False):
        self.config = Config()
        self.hatari = Hatari()
        # just instantiate all UI windows/widgets...
        self.hatariparent = None
        mainwin = self.create_mainwin(embed)
        if fullscreen:
            mainwin.fullscreen()
        self.create_dialogs(mainwin)
        mainwin.show_all()
    
    def run(self):
        gtk.main()

    def create_mainwin(self, embed):
        # main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.connect("delete_event", self.prevent_mainwin_close)
        mainwin.set_icon_from_file(self.icon)
        mainwin.set_title(self.title)
        
        # add buttons
        vbox = gtk.VBox()
        buttons = [
            ("Run Hatari!", self.run_clicked),
            ("Pause", self.pause_clicked),
            ("Configure", self.configure_clicked),
            ("About", self.about_clicked),
            ("Quit", self.quit_clicked)
        ]
        for label,cb in buttons:
            button = gtk.Button(label)
            # important, without these Hatari doesn't receive key events!
            button.unset_flags(gtk.CAN_FOCUS)
            button.connect("clicked", cb)
            vbox.add(button)
        # what to add to mainwindow
        if embed:
            hbox = gtk.HBox()
            self.hatariparent = self.create_socket()
            hbox.add(self.hatariparent)
            hbox.add(vbox)
            mainwin.add(hbox)
        else:
            mainwin.add(vbox)
        return mainwin
    
    def create_socket(self):
        # add Hatari parent container
        socket = gtk.Socket()
        # without this closing Hatari would remove the socket
        socket.connect("plug-removed", connect_true)
        socket.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("black"))
        socket.set_size_request(self.hatari_wd, self.hatari_ht)
        socket.set_events(gtk.gdk.ALL_EVENTS_MASK)
        socket.set_flags(gtk.CAN_FOCUS)
        return socket
    
    def create_dialogs(self, parent):
        # load UI dialogs from glade file
        wtree = gtk.glade.XML(self.gladefile)
        self.aboutdialog = wtree.get_widget("aboutdialog")
        self.confdialog = wtree.get_widget("confdialog")
        self.quitdialog = wtree.get_widget("quitdialog")
        self.killdialog = wtree.get_widget("killdialog")
        # modal dialogs need to be transient to their parents
        self.aboutdialog.set_transient_for(parent)
        self.confdialog.set_transient_for(parent)
        self.quitdialog.set_transient_for(parent)
        self.killdialog.set_transient_for(parent)
    
    def keep_hatari_running(self):
        if not self.hatari.is_running():
            return False
        # Hatari is running, OK to kill?
        response = self.killdialog.run()
        self.killdialog.hide()
        if response == gtk.RESPONSE_OK:
            self.hatari.stop()
            return False
        return True
    
    def run_clicked(self, widget):
        if self.keep_hatari_running():
            return
        if self.hatariparent:
            self.hatari.run(self.hatariparent.window)
        else:
            self.hatari.run()

    def prevent_mainwin_close(self, widget, arg):
        if self.keep_hatari_running():
            return True
        if self.config.is_changed():
            if self.quitdialog.run() == gtk.RESPONSE_OK:
                gtk.main_quit()
                # continue to mainwin destroy if called by delete_event
                return False
            self.quitdialog.hide()
            return True
        return False

    def quit_clicked(self, widget):
        self.prevent_mainwin_close(None, None)
        
    def about_clicked(self, widget):
        self.aboutdialog.run()
        self.aboutdialog.hide()

    def configure_clicked(self, widget):
        self.confdialog.run()
        self.confdialog.hide()
        print "TODO: configure Hatari accordingly"

    def pause_clicked(self, widget):
        if self.hatari.pause():
            widget.set_label("Continue\n(paused)")
        else:
            self.hatari.unpause()
            widget.set_label("Pause")


def usage(msg):
    print "\nusage: %s [-f]" % os.path.basename(sys.argv[0])
    print "\noptions:"
    print "\t-f\tstart in fullscreen"
    print "\t-e\tembed Hatari window"
    print "\nERROR: %s\n" % msg
    sys.exit(1)

if __name__ == "__main__":
    embed = False
    fullscreen = False
    for arg in sys.argv[1:]:
        if arg == "-f":
            fullscreen = True
        elif arg == "-e":
            embed = True
        else:
            usage("unknown option '%s'" % arg)
    app = HatariUI(fullscreen, embed)
    app.run()
