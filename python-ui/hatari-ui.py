#!/usr/bin/env python
#
# A PyGtk UI launcher that can embed the Hatari emulator window.
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
import getopt

# use correct version of pygtk/gtk
import pygtk
pygtk.require('2.0')
import gtk
import gtk.glade

from hatari import Hatari, Config


class Shortcuts():
    def __init__(self):
        self.all_types = ["fastforward", "frameskip", "debug", "trace"]
        self.tracedialog = None
        self.use_types = None
        self.control = None

    def set_parent(self, parent):
        self.parent = parent

    # ------- control -----------
    def set_control(self, control):
        self.control = control

    def change_option(self, option):
        if self.control:
            self.control.send("hatari-option %s\n" % option)
        else:
            print "ERROR: missing Hatari control"

    def trigger_shortcut(self, shortcut):
        if self.control:
            self.control.send("hatari-shortcut %s\n" % shortcut)
        else:
            print "ERROR: missing Hatari control"

    # ----- action types ---------
    def set_types(self, types):
        for usetype in types:
            if usetype not in self.all_types:
                return False
        self.use_types = types
        return True
        
    def list_all_types(self):
        # generate the list from class internal documentation
        for methodname in self.all_types:
            yield (methodname, Shortcuts.__dict__[methodname].__doc__)

    # ------- debug -----------
    def debug_cb(self, widget):
        print "Entering debug mode"
        self.change_option("--debug")
        self.trigger_shortcut("debug")

    def debug(self, config, horizontal):
        "Activate Hatari debug mode"
        widget = gtk.Button("Debug")
        widget.connect("clicked", self.debug_cb)
        return widget

    # ------- trace -----------
    def trace_cb(self, widget):
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
        RESPONSE_CLEAR_ALL = 1  # builtin ones are negative
        if not self.tracedialog:
            self.tracedialog = gtk.Dialog("Select trace points", self.parent,
                gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT)
            self.tracedialog.add_button("Clear all", RESPONSE_CLEAR_ALL)
            self.tracedialog.add_button(gtk.STOCK_APPLY, gtk.RESPONSE_APPLY)
            self.tracedialog.vbox.add(gtk.Label("Select trace points:"))
            self.tracewidgets = {}
            for trace in tracepoints:
                widget = gtk.CheckButton(trace)
                self.tracewidgets[trace] = widget
                self.tracedialog.vbox.add(widget)
            self.tracedialog.vbox.show_all()

        while True:
            response = self.tracedialog.run()
            if response == RESPONSE_CLEAR_ALL:
                for trace in tracepoints:
                    self.tracewidgets[trace].set_active(False)
                continue
            break

        if response == gtk.RESPONSE_APPLY:
            traces = []
            for trace in tracepoints:
                if self.tracewidgets[trace].get_active():
                    traces.append(trace)
            if traces:
                self.change_option("--trace %s" % ",".join(traces))
            else:
                self.change_option("--trace none")

        self.tracedialog.hide()

    def trace(self, config, horizontal):
        "Hatari tracing setup"
        widget = gtk.Button("Trace settings")
        widget.connect("clicked", self.trace_cb)
        return widget

    # ------- fast forward -----------
    def fastforward_cb(self, widget):
        if widget.get_active():
            print "Entering hyperspace!"
            self.change_option("--fast-forward on")
        else:
            print "Returning to normal speed"
            self.change_option("--fast-forward off")

    def fastforward(self, config, horizontal):
        "Whether to run Hatari at max speed"
        widget = gtk.CheckButton("Fast Forward")
        if config.get("[System]", "bFastForward") != "0":
            widget.set_active(True)
        widget.connect("toggled", self.fastforward_cb)
        return widget
    
    # ------- frame skip -----------
    def frameskip_cb(self, widget):
        if not self.control:
            print "ERROR: missing Hatari control"
        frameskips = widget.get_value()
        print "New frameskip value:", frameskips
        self.change_option("--frameskips %d" % frameskips)

    def frameskip(self, config, horizontal):
        "Increase/decrease screen update skip"
        box = gtk.HBox()
        label = gtk.Label("Frameskip:")
        box.pack_start(label, False, False, 0)
        if horizontal:
            widget = gtk.HScale()
        else:
            widget = gtk.VScale()
        widget.set_range(0, 8)
        widget.set_digits(0)
        frameskips = config.get("[Screen]", "FrameSkips")
        if frameskips:
            widget.set_value(int(frameskips))
        widget.connect("value-changed", self.frameskip_cb)
        # important, without this Hatari doesn't receive key events!
        widget.unset_flags(gtk.CAN_FOCUS)
        box.add(widget)
        return box

    # ------- widget box -----------
    def get_box(self, config, horizontal):
        "return Gtk Box container with the specified shortcut widgets"
        if not self.use_types:
            return None
        if horizontal:
            box = gtk.HBox()
        else:
            box = gtk.VBox()
        # TODO: get actual widgets & callbacks instead of these dummies
        for methodname in self.use_types:
            widget = Shortcuts.__dict__[methodname](self, config, horizontal)
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            box.add(widget)
        return box


class HatariUI():
    title = "Hatari UI v0.3"
    icon = "hatari-icon.png"
    gladefile = "hatari-ui.glade"
    hatari_wd = 640
    hatari_ht = 400

    def __init__(self, shortcuts, fullscreen, embed):
        self.config = Config()
        self.hatari = Hatari()
        self.shortcuts = shortcuts
        # just instantiate all UI windows/widgets...
        self.hatariparent = None
        mainwin = self.create_mainwin(embed)
        self.shortcuts.set_parent(mainwin)
        if fullscreen:
            mainwin.fullscreen()
        self.create_dialogs(mainwin)
        mainwin.show_all()
    
    def run(self):
        gtk.main()

    def create_mainwin(self, embed):
        # main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.connect("delete_event", self.quit_clicked)
        mainwin.set_icon_from_file(self.icon)
        mainwin.set_title(self.title)
        
        # add main buttons
        buttonbox = gtk.VBox()
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
            buttonbox.add(button)

        # what to add to mainwindow
        shortcuts = self.shortcuts.get_box(self.config, embed)
        if embed:
            vbox = gtk.VBox()
            hbox = gtk.HBox()
            self.hatariparent = self.create_socket()
            # make sure socket isn't resized
            hbox.pack_start(self.hatariparent, False, False, 0)
            hbox.add(buttonbox)
            vbox.add(hbox)
            if shortcuts:
                vbox.add(shortcuts)
            mainwin.add(vbox)
        elif shortcuts:
            hbox = gtk.HBox()
            hbox.add(shortcuts)
            hbox.add(buttonbox)
            mainwin.add(hbox)
        else:
            mainwin.add(buttonbox)
        return mainwin

    def connect_true(self, object):
        # disable Socket widget being destroyed on Plug (=Hatari) disappearance
        return True
    
    def create_socket(self):
        # add Hatari parent container
        socket = gtk.Socket()
        # without this closing Hatari would remove the socket
        socket.connect("plug-removed", self.connect_true)
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
        self.hatari.create_control()
        if self.hatariparent:
            control = self.hatari.run(self.config, self.hatariparent.window)
        else:
            control = self.hatari.run(self.config)
        self.shortcuts.set_control(control)

    def quit_clicked(self, widget, arg = None):
        if self.keep_hatari_running():
            return True
        if self.config.is_changed():
            if self.quitdialog.run() != gtk.RESPONSE_OK:
                self.quitdialog.hide()
                return True
        gtk.main_quit()
        # continue to mainwin destroy if called by delete_event
        return False
    
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


def usage(msg=None):
    name = os.path.basename(sys.argv[0])
    print "\nusage: %s [options]" % name
    print "\noptions:"
    print "\t-e, --embed\t\tembed Hatari window"
    print "\t-f, --fullscreen\tstart in fullscreen"
    print "\t-s, --shortcut <action>\tadd button for shortcut action"
    print "\nshortcut actions:"
    shortcuts = Shortcuts()
    for item in shortcuts.list_all_types():
        print "\t%s\t%s" % item
    print "\nexample:"
    print "\t%s -f -e -s fastforward -s frameskip" % name
    if msg:
        print "\nERROR: %s\n" % msg
    print
    sys.exit(1)

def main():
    embed = False
    fullscreen = False
    shortcuts = Shortcuts()
    try:
        longopts = ["embed", "fullscreen", "help", "shortcut="]
        opts, args = getopt.getopt(sys.argv[1:], "efhs:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(err)
    types = []
    for o, a in opts:
        if o in ("-e", "--embed"):
            embed = True
        elif o in ("-f", "--fullscreen"):
            fullscreen = True
        elif o in ("-h", "--help"):
            usage()
        elif o in ("-s", "--shortcut"):
                types.append(a)
        else:
            assert False, "getopt returned unhandled option"
    if not shortcuts.set_types(types):
        usage("unknown shortcut type")

    app = HatariUI(shortcuts, fullscreen, embed)
    app.run()

if __name__ == "__main__":
    main()
