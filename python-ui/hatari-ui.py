#!/usr/bin/env python
#
# A PyGtk UI launcher that can embed the Hatari emulator window.
#
# Requires PyGtk (python-gtk2) package and its dependencies to be present.
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
import gobject

from hatari import Hatari, Config
from dialogs import AboutDialog, KillDialog, QuitDialog, TraceDialog


class HatariUI():
    version = "v0.4"
    name = "Hatari UI"
    icon = "hatari-icon.png"
    hatari_wd = 640
    hatari_ht = 400
    all_controls = [
        "about", "run", "pause", "quit",
        "rightclick", "doubleclick",
        "fastforward", "frameskip",
        "debug", "trace"
    ]
    def __init__(self):
        # dialogs are created when needed
        self.aboutdialog = None
        self.killdialog = None
        self.quitdialog = None
        self.tracedialog = None
        # no controls set yet
        self.controls_left = None
        self.controls_right = None
        self.controls_top = None
        self.controls_bottom = None
        self.control_spacing = 4 # pixels

    # ----- control types ---------
    def set_controls(self, control_str, side):
        "return None as OK, error string if controls or side isn't recognized"
        controls = control_str.split(",")
        for control in controls:
            if control not in self.all_controls:
                if control.find("=") < 0:
                    return "unrecognized control '%s'" % control
                try:
                    # part after "=" converts to an int?
                    value = int(control.split("=")[1], 0)
                except ValueError:
                    return "invalid value in '%s'" % control
        if side == "left":
            self.controls_left = controls
        elif side == "right":
            self.controls_right = controls
        elif side == "top":
            self.controls_top = controls
        elif side == "bottom":
            self.controls_bottom = controls
        else:
            return "unknown controls position '%s'" % side
        return None
        
    def list_all_controls(self):
        # generate the list from class internal documentation
        for methodname in self.all_controls:
            yield (methodname, HatariUI.__dict__[methodname].__doc__)
        yield ("<name>=<code>", "<name> button simulates key <code> press/release")

    # ------- about control -----------
    def about_cb(self, widget):
        if not self.aboutdialog:
            self.aboutdialog = AboutDialog(self.mainwin, self.name, self.version)
        self.aboutdialog.run()
        self.aboutdialog.hide()

    def about(self):
        "Hatari UI information"
        return self.create_button("About", self.about_cb)
    
    # ------- run control -----------
    def run_cb(self, widget=None):
        if self.keep_hatari_running():
            return
        if self.hatariparent:
            self.hatari.run(self.config, self.hatariparent.window)
        else:
            self.hatari.run(self.config)

    def run(self):
        "(Re-)run Hatari"
        return self.create_button("Run Hatari!", self.run_cb)

    # ------- pause control -----------
    def pause_cb(self, widget):
        if self.hatari.pause():
            widget.set_label("Continue\n(paused)")
        else:
            self.hatari.unpause()
            widget.set_label("Pause")

    def pause(self):
        "Pause Hatari"
        return self.create_button("Pause", self.pause_cb)

    # ------- quit control -----------
    def quit_cb(self, widget, arg = None):
        if self.keep_hatari_running():
            return True
        if self.config.is_changed():
            if not self.quitdialog:
                self.quitdialog = QuitDialog(self.mainwin)
            if self.quitdialog.run() != gtk.RESPONSE_OK:
                self.quitdialog.hide()
                return True
        gtk.main_quit()
        # continue to mainwin destroy if called by delete_event
        return False

    def quit(self):
        "Quit Hatari UI"
        return self.create_button("Quit", self.quit_cb)

    # ------- doubleclick control -----------
    def doubleclick_cb(self, widget):
        self.hatari.insert_event("doubleclick")

    def doubleclick(self):
        "Simulate Atari left button double-click"
        return self.create_button("Doubleclick", self.doubleclick_cb)

    # ------- rightclick control -----------
    def rightpress_cb(self, widget):
        self.hatari.insert_event("rightpress")

    def rightrelease_cb(self, widget):
        self.hatari.insert_event("rightrelease")

    def rightclick(self):
        "Simulate Atari right button click"
        widget = gtk.Button("Rightclick")
        widget.connect("pressed", self.rightpress_cb)
        widget.connect("released", self.rightrelease_cb)
        return widget

    # ------- insert key control -----------
    def keypress_cb(self, widget, code):
        self.hatari.insert_event("keypress %s" % code)

    def keyrelease_cb(self, widget, code):
        self.hatari.insert_event("keyrelease %s" % code)

    def create_key_control(self, namecode):
        "Simulate Atari key press/release"
        offset = namecode.rfind("=")
        name = namecode[:offset]
        code = namecode[offset+1:]
        widget = gtk.Button(name)
        widget.connect("pressed", self.keypress_cb, code)
        widget.connect("released", self.keyrelease_cb, code)
        return widget

    # ------- debug control -----------
    def debug_cb(self, widget):
        print "Entering debug mode"
        self.hatari.change_option("--debug")
        self.hatari.trigger_shortcut("debug")

    def debug(self):
        "Activate Hatari debug mode"
        return self.create_button("Debug", self.debug_cb)

    # ------- trace control -----------
    def trace_cb(self, widget):
        if not self.tracedialog:
            self.tracedialog = TraceDialog(self.mainwin, self.hatari)
        self.tracedialog.run()
        self.tracedialog.hide()

    def trace(self):
        "Hatari tracing setup"
        return self.create_button("Trace settings", self.trace_cb)

    # ------- fast forward control -----------
    def fastforward_cb(self, widget):
        if widget.get_active():
            print "Entering hyper speed!"
            self.hatari.change_option("--fast-forward on")
        else:
            print "Returning to normal speed"
            self.hatari.change_option("--fast-forward off")

    def fastforward(self):
        "Whether to fast forward Hatari (needs fast machine)"
        widget = gtk.CheckButton("Forward")
        if self.config.get("[System]", "bFastForward") != "0":
            widget.set_active(True)
        widget.connect("toggled", self.fastforward_cb)
        return widget
    
    # ------- frame skip control -----------
    def frameskip_cb(self, widget):
        frameskips = widget.get_value()
        print "New frameskip value:", frameskips
        self.hatari.change_option("--frameskips %d" % frameskips)

    def frameskip(self):
        "Increase/decrease screen update skip"
        if self.to_horizontal_box:
            box = gtk.HBox(False, self.control_spacing/2)
            box.pack_start(gtk.Label("Frameskip:"), False, False, 0)
            widget = gtk.HScale()
        else:
            box = gtk.VBox()
            box.pack_end(gtk.Label("Frameskip"), False, False, 0)
            widget = gtk.VScale()
        widget.set_range(0, 8)
        widget.set_digits(0)
        frameskips = self.config.get("[Screen]", "nFrameSkips")
        if frameskips:
            widget.set_value(int(frameskips))
        widget.connect("value-changed", self.frameskip_cb)
        # important, without this Hatari doesn't receive key events!
        widget.unset_flags(gtk.CAN_FOCUS)
        box.add(widget)
        return box

    # ------- control widget box -----------
    def get_control_box(self, controls, horizontal):
        "return Gtk Box container with the specified control widgets or None for no controls"
        if not controls:
            return None
        self.to_horizontal_box = horizontal
        if horizontal:
            box = gtk.HBox(False, self.control_spacing)
        else:
            box = gtk.VBox(False, self.control_spacing)
        for control in controls:
            if control.find("=") >= 0:
                # handle "<name>=<keycode>" control specification
                widget = self.create_key_control(control)
            else:
                widget = HatariUI.__dict__[control](self)
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            # widen widgets other than checkboxes
            if widget.__gtype__ == gtk.CheckButton.__gtype__:
                box.pack_start(widget, False, False, 0)
            else:
                box.pack_start(widget, True, True, 0)
        return box


    # ---------- create UI ----------------
    def create_ui(self, fullscreen, embed):
        self.config = Config()
        self.hatari = Hatari()
        # just instantiate all UI windows/widgets...
        self.hatariparent = None
        self.mainwin = self.create_mainwin(embed)
        if fullscreen:
            self.mainwin.fullscreen()

    def create_mainwin(self, embed):
        # create control button rows/columns
        if not (self.controls_left or self.controls_right or self.controls_top or self.controls_bottom):
            self.controls_bottom = ["about", "pause", "quit"]
        left   = self.get_control_box(self.controls_left, False)
        right  = self.get_control_box(self.controls_right, False)
        top    = self.get_control_box(self.controls_top, True)
        bottom = self.get_control_box(self.controls_bottom, True)
        # add horizontal elements
        hbox = gtk.HBox()
        if left:
            hbox.add(left)
        if embed:
            self.hatariparent = self.create_socket()
            # make sure socket isn't resized
            hbox.pack_start(self.hatariparent, False, False, 0)
        if right:
            hbox.add(right)
        # add vertical elements
        vbox = gtk.VBox()
        if top:
            vbox.add(top)
        vbox.add(hbox)
        if bottom:
            vbox.add(bottom)
        # put them to main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.connect("delete_event", self.quit_cb)
        mainwin.set_title("%s %s" % (self.name, self.version))
        mainwin.set_icon_from_file(self.icon)
        mainwin.add(vbox)
        return mainwin

    def plug_remove_cb(self, object):
        # disable Socket widget being destroyed on Plug (=Hatari) disappearance
        return True
    
    def create_socket(self):
        # add Hatari parent container
        socket = gtk.Socket()
        # without this closing Hatari would remove the socket
        socket.connect("plug-removed", self.plug_remove_cb)
        socket.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("black"))
        socket.set_size_request(self.hatari_wd, self.hatari_ht)
        socket.set_events(gtk.gdk.ALL_EVENTS_MASK)
        socket.set_flags(gtk.CAN_FOCUS)
        return socket

    # ------- helper methods -----------
    def create_button(self, label, cb):
        widget = gtk.Button(label)
        widget.connect("clicked", cb)
        return widget
    
    def keep_hatari_running(self):
        if not self.hatari.is_running():
            return False
        
        if not self.killdialog:
            self.killdialog = KillDialog(self.mainwin)
        # Hatari is running, OK to kill?
        response = self.killdialog.run()
        self.killdialog.hide()
        if response == gtk.RESPONSE_OK:
            self.hatari.stop()
            return False
        return True
    
    def main(self):
        self.mainwin.show_all()
        gobject.idle_add(self.run_cb)
        gtk.main()


def usage(app, msg=None):
    name = os.path.basename(sys.argv[0])
    appname = "%s %s" % (app.name, app.version)
    print "\n%s" % appname
    print "=" * len(appname)
    print "\nUsage: %s [options]" % name
    print "\nOptions:"
    print "\t-e, --embed\t\tembed Hatari window in middle of controls"
    print "\t-f, --fullscreen\tstart in fullscreen"
    print "\t-h, --help\t\tthis help"
    print "\t-l, --left <controls>\tleft UI controls"
    print "\t-r, --right <controls>\tright UI controls"
    print "\t-t, --top <controls>\ttop UI controls"
    print "\t-b, --bottom <controls>\tbottom UI controls"
    print "\t-s, --spacing <pixels>\tspacing between controls in pixels"
    print "\nif no options are given, controls given in example below are used."
    print "\nAvailable controls:"
    for control, description in app.list_all_controls():
        if len(control) < 8:
            tabs = "\t\t"
        else:
            tabs = "\t"
        print "\t%s%s%s" % (control, tabs, description)
    print "\nExample:"
    print "\t%s -e --top run,Undo=0x61,about,quit -s 8\n" % name
    if msg:
        print "ERROR: %s\n" % msg
    sys.exit(1)


def main():
    app = HatariUI()
    try:
        longopts = ["embed", "fullscreen", "help", "left=", "right=", "top=", "bottom=", "spacing="]
        opts, args = getopt.getopt(sys.argv[1:], "efhl:r:t:b:s:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(app, err)

    error = None
    embed = False
    fullscreen = False
    for o, a in opts:
        print o, a
        if o in ("-e", "--embed"):
            embed = True
        elif o in ("-f", "--fullscreen"):
            fullscreen = True
        elif o in ("-h", "--help"):
            usage(app)
        elif o in ("-l", "--left"):
            error = app.set_controls(a, "left")
        elif o in ("-r", "--right"):
            error = app.set_controls(a, "right")
        elif o in ("-t", "--top"):
            error = app.set_controls(a, "top")
        elif o in ("-b", "--bottom"):
            error = app.set_controls(a, "bottom")
        elif o in ("-s", "--spacing"):
            app.control_spacing = int(a, 0)
        else:
            assert False, "getopt returned unhandled option"
        if error:
            usage(app, error)

    app.create_ui(fullscreen, embed)
    app.main()

if __name__ == "__main__":
    main()
