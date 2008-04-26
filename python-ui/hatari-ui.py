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
import gobject

from hatari import Hatari, Config


class HatariUI():
    version = "v0.4"
    name = "Hatari UI"
    icon = "hatari-icon.png"
    logo = "hatari.png"
    hatari_wd = 640
    hatari_ht = 400
    all_controls = [
        "about", "run", "pause", "quit",
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
        controls = control_str.split(",")
        for control in controls:
            if control not in self.all_controls:
                return False
        if side == "left":
            self.controls_left = controls
        elif side == "right":
            self.controls_right = controls
        elif side == "top":
            self.controls_top = controls
        elif side == "bottom":
            self.controls_bottom = controls
        else:
            return False
        return True
        
    def list_all_controls(self):
        # generate the list from class internal documentation
        for methodname in self.all_controls:
            yield (methodname, HatariUI.__dict__[methodname].__doc__)

    # ------- about control -----------
    def about_cb(self, widget):
        if not self.aboutdialog:
            dialog = gtk.AboutDialog()
            dialog.set_name(self.name)
            dialog.set_version(self.version)
            dialog.set_website("http://hatari.sourceforge.net/")
            dialog.set_website_label("Hatari emulator www-site")
            dialog.set_authors(["Eero Tamminen"])
            dialog.set_artists(["The logo is from Hatari"])
            dialog.set_logo(gtk.gdk.pixbuf_new_from_file(self.logo))
            dialog.set_translator_credits("translator-credits")
            dialog.set_copyright("UI copyright (C) 2008 by Eero Tamminen")
            dialog.set_license("""
This software is licenced under GPL v2.

You can see the whole license at:
    http://www.gnu.org/licenses/info/GPLv2.html""")
            dialog.set_transient_for(self.mainwin)
            self.aboutdialog = dialog
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
                self.quitdialog = gtk.MessageDialog(self.mainwin,
                    gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                    gtk.MESSAGE_QUESTION, gtk.BUTTONS_OK_CANCEL,
                    "You have unsaved changes.\n\nQuit anyway?")
            if self.quitdialog.run() != gtk.RESPONSE_OK:
                self.quitdialog.hide()
                return True
        gtk.main_quit()
        # continue to mainwin destroy if called by delete_event
        return False

    def quit(self):
        "Quit Hatari UI"
        return self.create_button("Quit", self.quit_cb)

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
            self.tracedialog = gtk.Dialog("Select trace points", self.mainwin,
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
                self.hatari.change_option("--trace %s" % ",".join(traces))
            else:
                self.hatari.change_option("--trace none")

        self.tracedialog.hide()

    def trace(self):
        "Hatari tracing setup"
        return self.create_button("Trace settings", self.trace_cb)

    # ------- fast forward control -----------
    def fastforward_cb(self, widget):
        if widget.get_active():
            print "Entering hyperspace!"
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
    def get_box(self, controls, horizontal):
        "return Gtk Box container with the specified control widgets or None for no controls"
        if not controls:
            return None
        self.to_horizontal_box = horizontal
        if horizontal:
            box = gtk.HBox(False, self.control_spacing)
        else:
            box = gtk.VBox(False, self.control_spacing)
        for methodname in controls:
            widget = HatariUI.__dict__[methodname](self)
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
        if not (self.controls_left or self.controls_right or self.controls_top or self.controls_right):
            self.controls_bottom = ["about", "pause", "quit"]
        left   = self.get_box(self.controls_left, False)
        right  = self.get_box(self.controls_right, False)
        top    = self.get_box(self.controls_top, True)
        bottom = self.get_box(self.controls_bottom, True)
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
            self.killdialog = gtk.MessageDialog(self.mainwin,
                gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                gtk.MESSAGE_QUESTION, gtk.BUTTONS_OK_CANCEL,
                "Hatari emulator is already/still running and it needs to be terminated first. However, if it contains unsaved data, that will be lost.\n\nTerminate Hatari anyway?")
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
    print
    print "\t-l, --left <controls>\tleft UI controls"
    print "\t-r, --right <controls>\tright UI controls"
    print "\t-t, --top <controls>\ttop UI controls"
    print "\t-b, --bottom <controls>\tbottom UI controls"
    print "\t-s, --spacing\tspacing between controls (in pixels)"
    print "\nif no options are given, controls given in example below are used."
    print "\nAvailable controls:"
    for control, description in app.list_all_controls():
        if len(control) < 8:
            tabs = "\t\t"
        else:
            tabs = "\t"
        print "\t%s%s%s" % (control, tabs, description)
    print "\nExample:"
    print "\t%s --embed --bottom run,about,quit\n" % name
    if msg:
        print "ERROR: %s\n" % msg
    sys.exit(1)


def main():
    embed = False
    fullscreen = False
    app = HatariUI()
    try:
        longopts = ["embed", "fullscreen", "help", "left", "right", "top", "bottom", "spacing"]
        opts, args = getopt.getopt(sys.argv[1:], "efhl:r:t:b:s:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(app, err)
    for o, a in opts:
        if o in ("-e", "--embed"):
            embed = True
        elif o in ("-f", "--fullscreen"):
            fullscreen = True
        elif o in ("-h", "--help"):
            usage(app)

        elif o in ("-l", "--left"):
            if not app.set_controls(a, "left"):
                usage(app, "unrecognized control(s) given")
        elif o in ("-r", "--right"):
            if not app.set_controls(a, "right"):
                usage(app, "unrecognized control(s) given")
        elif o in ("-t", "--top"):
            if not app.set_controls(a, "top"):
                usage(app, "unrecognized control(s) given")
        elif o in ("-b", "--bottom"):
            if not app.set_controls(a, "bottom"):
                usage(app, "unrecognized control(s) given")
        elif o in ("-s", "--spacing"):
            app.control_spacing = int(a)

        else:
            assert False, "getopt returned unhandled option"

    app.create_ui(fullscreen, embed)
    app.main()

if __name__ == "__main__":
    main()
