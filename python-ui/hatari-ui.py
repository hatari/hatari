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

from hatari import Hatari, ConfigMapping
from dialogs import AboutDialog, PasteDialog, KillDialog, QuitSaveDialog,\
     SetupDialog, TraceDialog, HatariInsertText
from debugui import HatariDebugUI


class HatariUI():
    version = "v0.5"
    name = "Hatari UI"
    icon = "hatari-icon.png"
    hatari_wd = 640
    hatari_ht = 400
    all_controls = [
        "about", "run", "paste", "pause", "setup", "quit",
        "fastforward", "frameskip", "spec512", "sound",
        "rightclick", "doubleclick",
        "debug", "trace"
    ]
    def __init__(self):
        self.debugui = None

        # dialogs are created when needed
        self.aboutdialog = None
        self.killdialog = None
        self.pastedialog = None
        self.quitdialog = None
        self.setupdialog = None
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
                # not one of special cases?
                if control != "|" and control.find("=") < 0:
                    return "unrecognized control '%s'" % control

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
        yield ("|", "Separator between controls")
        for methodname in self.all_controls:
            yield (methodname, HatariUI.__dict__[methodname].__doc__)
        yield ("<name>=<string/code>", "Insert string or single key <code>")

    # ------- about control -----------
    def about_cb(self, widget):
        if not self.aboutdialog:
            self.aboutdialog = AboutDialog(self.mainwin, self.name, self.version)
        self.aboutdialog.run()

    def about(self):
        "Hatari UI information"
        return self.create_button("About", self.about_cb)
    
    # ------- run control -----------
    def run_cb(self, widget=None):
        if self.keep_hatari_running():
            return
        if self.hatariparent:
            size = self.hatariparent.window.get_size()
            self.hatari.run(self.hatariparent.window, self.config.get_embed_args(size))
        else:
            self.hatari.run()

    def run(self):
        "(Re-)run Hatari"
        return self.create_button("Run Hatari!", self.run_cb)

    # ------- paste control -----------
    def paste_cb(self, widget):
        if not self.pastedialog:
            self.pastedialog = PasteDialog(self.mainwin)
        text = self.pastedialog.run()
        if text:
            HatariInsertText(self.hatari, text)

    def paste(self):
        "Insert text to Hatari window"
        return self.create_button("Paste", self.paste_cb)

    # ------- pause control -----------
    def pause_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
        else:
            self.hatari.unpause()

    def pause(self):
        "Pause Hatari to save battery"
        widget = gtk.ToggleButton("Stop")
        widget.connect("toggled", self.pause_cb)
        return (widget, True)

    # ------- setup control -----------
    def setup_cb(self, widget):
        if not self.setupdialog:
            self.setupdialog = SetupDialog(self.mainwin, self.hatari)
        self.setupdialog.run()

    def setup(self):
        "Hatari configuration setup"
        return self.create_button("Hatari setup", self.setup_cb)

    # ------- quit control -----------
    def quit_cb(self, widget, arg = None):
        if self.keep_hatari_running():
            return True
        if self.config.is_changed():
            if not self.quitdialog:
                self.quitdialog = QuitSaveDialog(self.mainwin, self.config)
            if self.quitdialog.run() == gtk.RESPONSE_CANCEL:
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
        return (widget, True)

    # ------- debug control -----------
    def debug_cb(self, widget):
        if not self.debugui:
            self.debugui = HatariDebugUI(self.hatari, self.icon)
        self.debugui.show()

    def debug(self):
        "Activate Hatari debug mode"
        return self.create_button("Debug", self.debug_cb)

    # ------- trace control -----------
    def trace_cb(self, widget):
        if not self.tracedialog:
            self.tracedialog = TraceDialog(self.mainwin, self.hatari)
        self.tracedialog.run()

    def trace(self):
        "Hatari tracing setup"
        return self.create_button("Trace settings", self.trace_cb)

    # ------- fast forward control -----------
    def fastforward_cb(self, widget):
        self.config.set_fastforward(widget.get_active())

    def fastforward(self):
        "Whether to fast forward Hatari (needs fast machine)"
        widget = gtk.CheckButton("FastForward")
        widget.set_active(self.config.get_fastforward())
        widget.connect("toggled", self.fastforward_cb)
        return (widget, False)

    # ------- spec512 control -----------
    def spec512_cb(self, widget):
        self.config.set_spec512threshold(widget.get_active())

    def spec512(self):
        "Whether to support Spec512 (>16 colors at the same time)"
        widget = gtk.CheckButton("Spec512 support")
        widget.set_active(self.config.get_spec512threshold())
        widget.connect("toggled", self.spec512_cb)
        return (widget, False)

    # ------- sound control -----------
    def sound_cb(self, widget):
        self.config.set_sound(widget.get_active())

    def sound(self):
        "Select sound quality"
        combo = gtk.combo_box_new_text()
        for text in self.config.get_sound_values():
            combo.append_text(text)
        combo.set_active(self.config.get_sound())
        combo.connect("changed", self.sound_cb)
        if self.to_horizontal_box:
            box = gtk.HBox(False, self.control_spacing/2)
        else:
            box = gtk.VBox()
        box.pack_start(gtk.Label("Sound:"), False, False)
        box.add(combo)
        return (box, False)
    
    # ------- frame skip control -----------
    def frameskip_cb(self, widget):
        self.config.set_frameskips(widget.get_value())

    def frameskip(self):
        "Increase/decrease screen frame skip"
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
        widget.set_value(self.config.get_frameskips())
        widget.connect("value-changed", self.frameskip_cb)
        # important, without this Hatari doesn't receive key events!
        widget.unset_flags(gtk.CAN_FOCUS)
        box.add(widget)
        return (box, True)

    # ------- insert key control -----------
    def keypress_cb(self, widget, code):
        self.hatari.insert_event("keypress %s" % code)

    def keyrelease_cb(self, widget, code):
        self.hatari.insert_event("keyrelease %s" % code)

    def textinsert_cb(self, widget, text):
        HatariInsertText(self.hatari, text)

    def create_key_control(self, namecode):
        "Simulate Atari key press/release and string inserting"
        offset = namecode.find("=")
        text = namecode[offset+1:]
        name = namecode[:offset]
        widget = gtk.Button(name)
        try:
            # part after "=" converts to an int?
            code = int(text, 0)
            widget.connect("pressed", self.keypress_cb, code)
            widget.connect("released", self.keyrelease_cb, code)
            tip = "keycode: %d" % code
        except ValueError:
            # no, assume a string macro is wanted instead
            widget.connect("clicked", self.textinsert_cb, text)
            tip = "string '%s'" % text
        return (widget, tip, True)

    # ------- control widget box -----------
    def add_tooltip(self, widget, text):
        w = widget
        # get the first non-label child of a container
        while w.__gtype__ in (gtk.HBox.__gtype__, gtk.VBox.__gtype__):
            children = widget.get_children()
            if not children:
                break
            for child in children:
                if child.__gtype__ not in (gtk.Label.__gtype__,):
                    break
            w = child
        #print w.__gtype__, "tooltip:", text
        self.tooltips.set_tip(w, text)

    def get_control_box(self, controls, horizontal):
        "return Gtk Box container with the specified control widgets or None for no controls"
        if not controls:
            return None
        self.to_horizontal_box = horizontal
        if horizontal:
            box = gtk.HBox(False, self.control_spacing)
        else:
            box = gtk.VBox(False, self.control_spacing)
        self.tooltips = gtk.Tooltips()
        for control in controls:
            if control == "|":
                if horizontal:
                    widget = gtk.VSeparator()
                else:
                    widget = gtk.HSeparator()
                expand = False
            elif control.find("=") >= 0:
                # handle "<name>=<keycode>" control specification
                (widget, tip, expand) = self.create_key_control(control)
                # TODO: for some reason tooltips don't work on these buttons?
                self.tooltips.set_tip(widget, "Insert " + tip)
            else:
                method = HatariUI.__dict__[control]
                (widget, expand) = method(self)
                self.add_tooltip(widget, method.__doc__)
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            box.pack_start(widget, expand, expand, 0)
        return box


    # ---------- create UI ----------------
    def create_ui(self, fullscreen, embed):
        self.hatari = Hatari()
        self.config = ConfigMapping(self.hatari)
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
            self.hatariparent = self.create_uisocket()
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
    
    def create_uisocket(self):
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
        "return (button with given label, expand as True)"
        widget = gtk.Button(label)
        widget.connect("clicked", cb)
        return (widget, True)
    
    def keep_hatari_running(self):
        if not self.hatari.is_running():
            return False
        
        if not self.killdialog:
            self.killdialog = KillDialog(self.mainwin)
        # Hatari is running, OK to kill?
        if self.killdialog.run() == gtk.RESPONSE_OK:
            self.hatari.kill()
            return False
        return True
    
    def main(self):
        self.mainwin.show_all()
        # Hatari window can be created only after Socket window is created
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
        size = len(control)
        if size < 8:
            tabs = "\t\t"
        elif size < 16:
            tabs = "\t"
        else:
            tabs = "\n\t\t\t"
        print "\t%s%s%s" % (control, tabs, description)
    print """
Example:
\t%s -e \\
\t-t "about,run,pause,quit" \\
\t-b "sound,spec512,|,fastforward,|,frameskip" \\
\t-l "Macro=Testing...,Undo=97,Help=98,Enter=114,F1=59,F2=60,F3=61,F4=62" \\
\t-r "paste,debug,trace,setup"
""" % name
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
