#!/usr/bin/env python
#
# A PyGtk UI for Hatari that can embed the Hatari emulator window.
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

from debugui import HatariDebugUI
from hatari import Hatari, HatariConfigMapping
from uihelpers import HatariTextInsert, UInfo, create_button, create_toggle
from dialogs import AboutDialog, PasteDialog, KillDialog, QuitSaveDialog, \
     ResetDialog, SetupDialog, TraceDialog, PeripheralsDialog, ErrorDialog, \
     DisplayDialog


# helper functions to match callback args
def window_hide_cb(window, arg):
    window.hide()
    return True


class UIActions:
    def __init__(self):
        self.about = gtk.Action("about", "About", "Hatari UI information", None)
        self.shot = gtk.Action("shot", "Screenshot", "Take a screenshot", None)
        self.quit = gtk.Action("quit", "Quit", "Quit Hatari UI", None)

        self.run = gtk.Action("run", "Run", "(Re-)run Hatari", None)
        self.pause = gtk.Action("pause", "Pause", "Pause Hatari to save battery", None)
        self.ff = gtk.ToggleAction("ff", "FastForward", "Whether to fast forward Hatari (needs fast machine)", None)
        self.full = gtk.ToggleAction("full", "Fullscreen", "Toggle whether Hatari is fullscreen", None)
        self.reset = gtk.Action("reset", "Reset...", "Warm or cold reset Hatari", None)

        self.sound = gtk.Action("sound", "Sound", "Sound settings", None)
        self.display = gtk.Action("display", "Display...", "Display settings", None)
        self.devices = gtk.Action("devices", "Peripherals...", "Floppy and joystick settings", None)
        self.machine = gtk.Action("machine", "Machine...", "Hatari st/e/tt/falcon configuration", None)

        self.paste = gtk.Action("paste", "Paste text...", "Insert text to Hatari window", None)
        self.dclick = gtk.Action("dclick", "Doubleclick", "Simulate Atari left button double-click", None)
        self.rclick = gtk.Action("rclick", "Rightclick", "Simulate Atari right button click", None)
    
        self.debug = gtk.Action("debug", "Debugger...", "Activate Hatari debugger", None)
        self.trace = gtk.Action("trace", "Trace settings...", "Hatari tracing setup", None)
        self.close = gtk.Action("close", "Close", "Close button for panel windows", None)
        
        self.show_list()

    def show_list(self):
        for act in (self.about, self.shot, self.quit,
                    self.run, self.pause,self.ff, self.full, self.reset,
                    self.sound, self.display, self.devices, self.machine,
                    self.dclick, self.rclick, self.paste,
                    self.debug, self.trace, self.close):
            print "%s\t%s" % (act.get_name(), act.get_property("tooltip"))

    def activate(self, widget):
        print "activated:", widget.get_name()
        
    def get_menu(self):
        allmenus = (
        ("File", (self.about, None, self.shot, None, self.quit)),
        ("Emulation", (self.run, self.pause, None, self.ff, None, self.full, None, self.reset)),
        ("Setup", (self.sound, self.display, self.devices, self.machine)),
        ("Input", (self.paste, self.dclick, self.rclick)),
        ("Debug", (self.debug, self.trace))
        )
        
        bar = gtk.MenuBar()

        for title, barmenu in allmenus:
            submenu = gtk.Menu()
            for action in barmenu:
                if action:
                    action.connect("activate", self.activate)
                    item = action.create_menu_item()
                else:
                    item = gtk.SeparatorMenuItem()
                submenu.add(item)
            baritem = gtk.MenuItem(title, False)
            baritem.set_submenu(submenu)
            bar.add(baritem)

        return bar


# class for all the controls that the different UI panels and windows can have
class HatariControls():
    # these are the names of methods returning (control widget, expand flag) tuples
    # which also give description of the co. control in their __doc__ attribute
    #
    # (in a more OO application all these widgets would be separate classes
    # inheriting a common interface class, but that would be an overkill)
    all = [
        "about", "run", "pause", "reset", "setup", "quit",
        "fullscreen", "fastforward", "display", "sound", "screenshot",
        "rightclick", "doubleclick", "paste", "peripherals",
        "debug", "trace", "close"
    ]
    # spacing between input widget and its label
    label_spacing = 4
    
    def __init__(self):
        self.hatari = Hatari()
        self.config = HatariConfigMapping(self.hatari)
        if not self.config.is_loaded():
            ErrorDialog(None).run("Loading Hatari configuration failed.\nMake sure you've saved one!")
            sys.exit(1)
        # TODO: Hatari UI configuration settings save/load
        self.tracepoints = None
        # set later by owner of this object
        self.hatariparent = None
        self.mainwin = None
        self.io_id = None
        # windows/dialogs are created when needed
        self.debugui = None
        self.aboutdialog = None
        self.resetdialog = None
        self.killdialog = None
        self.peripheralsdialog = None
        self.displaydialog = None
        self.pastedialog = None
        self.quitdialog = None
        self.setupdialog = None
        self.tracedialog = None
        # temporary settings
        self.to_horizontal_box = None

    def set_mainwin_hatariparent(self, mainwin, parent):
        # ugly, but I didn't come up with better place to connect quit_cb
        mainwin.connect("delete_event", self._quit_cb)
        self.killdialog = KillDialog(mainwin)
        self.mainwin = mainwin
        if parent:
            # set initial embedded hatari size
            width, height = self.config.get_window_size()
            parent.set_size_request(width, height)
        self.hatariparent = parent
        # Hatari window can be created only after Socket window is created.
        # also ugly to do here...
        gobject.idle_add(self._run_cb)
    
    def set_box_horizontal(self, horizontal):
        self.to_horizontal_box = horizontal

    # ------- about control -----------
    def _about_cb(self, widget):
        if not self.aboutdialog:
            self.aboutdialog = AboutDialog(self.mainwin)
        self.aboutdialog.run()

    def about(self):
        "Hatari UI information"
        return (create_button("About", self._about_cb), True)
    
    # ------- run control -----------
    def _socket_cb(self, fd, event):
        if event != gobject.IO_IN:
            # hatari process died, make sure Hatari instance notices
            self.hatari.kill()
            return False
        width, height = self.hatari.get_embed_info()
        print "New size = %d x %d" % (width, height)
        # TODO: setting window smaller than currently messes
        # up the automatic sizing when Hatari window size grows
        #oldwidth, oldheight = self.hatariparent.get_size_request()
        #if width < oldwidth or height < oldheight:
        #    # force mainwin smaller
        #    self.mainwin.set_size_request(width, height)
        self.hatariparent.set_size_request(width, height)
        return True

    def _run_cb(self, widget=None):
        if self.killdialog.run(self.hatari):
            return
        if self.io_id:
            gobject.source_remove(self.io_id)
        if self.hatariparent:
            size = self.hatariparent.window.get_size()
            self.hatari.run(None, self.hatariparent.window)
            # get notifications of Hatari window size changes
            self.hatari.enable_embed_info()
            socket = self.hatari.get_control_socket().fileno()
            events = gobject.IO_IN | gobject.IO_HUP | gobject.IO_ERR
            self.io_id = gobject.io_add_watch(socket, events, self._socket_cb)
        else:
            self.hatari.run()

    def run(self):
        "(Re-)run Hatari"
        return (create_button("Run", self._run_cb), True)

    # ------- close control -----------
    def _close_cb(self, widget):
        widget.get_toplevel().hide()
        
    def close(self):
        "Close button (makes panel window to a dialog)"
        return (create_button("Close", self._close_cb), True)

    # ------- paste control -----------
    def _paste_cb(self, widget):
        if not self.pastedialog:
            self.pastedialog = PasteDialog(self.mainwin)
        text = self.pastedialog.run()
        if text:
            HatariTextInsert(self.hatari, text)

    def paste(self):
        "Insert text to Hatari window"
        return (create_button("Paste text", self._paste_cb), True)

    # ------- pause control -----------
    def _pause_cb(self, widget):
        if widget.get_active():
            self.hatari.pause()
        else:
            self.hatari.unpause()

    def pause(self):
        "Pause Hatari to save battery"
        return (create_toggle("Pause", self._pause_cb), True)

    # ------- reset control -----------
    def _reset_cb(self, widget):
        if not self.resetdialog:
            self.resetdialog = ResetDialog(self.mainwin)
        self.resetdialog.run(self.hatari)

    def reset(self):
        "Warm or cold reset Hatari"
        return (create_button("Reset", self._reset_cb), True)

    # ------- setup control -----------
    def _setup_cb(self, widget):
        if not self.setupdialog:
            self.setupdialog = SetupDialog(self.mainwin)
        if self.setupdialog.run(self.config):
            self.hatari.trigger_shortcut("warmreset")

    def setup(self):
        "Hatari st/e/tt/falcon configuration"
        return (create_button("Machine setup", self._setup_cb), True)

    # ------- quit control -----------
    def _quit_cb(self, widget, arg = None):
        if self.killdialog.run(self.hatari):
            return True
        if self.io_id:
            gobject.source_remove(self.io_id)
        if self.config.is_changed():
            if not self.quitdialog:
                self.quitdialog = QuitSaveDialog(self.mainwin)
            if self.quitdialog.run(self.config) == gtk.RESPONSE_CANCEL:
                return True
        gtk.main_quit()
        # continue to mainwin destroy if called by delete_event
        return False

    def quit(self):
        "Quit Hatari UI"
        return (create_button("Quit", self._quit_cb), True)

    # ------- peripherals control -----------
    def _peripherals_cb(self, widget):
        if not self.peripheralsdialog:
            self.peripheralsdialog = PeripheralsDialog(self.mainwin)
        self.peripheralsdialog.run(self.config)

    def peripherals(self):
        "Dialog for Hatari peripherals settings"
        return (create_button("Peripherals", self._peripherals_cb), True)

    # ------- display control -----------
    def _display_cb(self, widget):
        if not self.displaydialog:
            self.displaydialog = DisplayDialog(self.mainwin)
        self.displaydialog.run(self.config)

    def display(self):
        "Dialog for Hatari display settings"
        return (create_button("Display", self._display_cb), True)

    # ------- doubleclick control -----------
    def _doubleclick_cb(self, widget):
        self.hatari.insert_event("doubleclick")

    def doubleclick(self):
        "Simulate Atari left button double-click"
        return (create_button("Doubleclick", self._doubleclick_cb), True)

    # ------- rightclick control -----------
    def _rightpress_cb(self, widget):
        self.hatari.insert_event("rightpress")

    def _rightrelease_cb(self, widget):
        self.hatari.insert_event("rightrelease")

    def rightclick(self):
        "Simulate Atari right button click"
        widget = gtk.Button("Rightclick")
        widget.connect("pressed", self._rightpress_cb)
        widget.connect("released", self._rightrelease_cb)
        return (widget, True)

    # ------- debug control -----------
    def _debug_cb(self, widget):
        if not self.debugui:
            self.debugui = HatariDebugUI(self.hatari)
        self.debugui.show()

    def debug(self):
        "Activate Hatari debugger"
        return (create_button("Debugger", self._debug_cb), True)

    # ------- trace control -----------
    def _trace_cb(self, widget):
        if not self.tracedialog:
            self.tracedialog = TraceDialog(self.mainwin)
        self.tracepoints = self.tracedialog.run(self.hatari, self.tracepoints)

    def trace(self):
        "Hatari tracing setup"
        return (create_button("Trace settings", self._trace_cb), True)

    # ------- fast forward control -----------
    def _fastforward_cb(self, widget):
        self.config.set_fastforward(widget.get_active())

    def fastforward(self):
        "Whether to fast forward Hatari (needs fast machine)"
        widget = gtk.CheckButton("FastForward")
        widget.set_active(self.config.get_fastforward())
        widget.connect("toggled", self._fastforward_cb)
        return (widget, False)

    # ------- fullscreen control -----------
    def _fullscreen_cb(self, widget):
        self.config.set_fullscreen(widget.get_active())

    def fullscreen(self):
        "Hatari window fullscreen toggle"
        widget = gtk.CheckButton("fullscreen")
        widget.set_active(self.config.get_fullscreen())
        widget.connect("toggled", self._fullscreen_cb)
        return (widget, False)

    # ------- sound control -----------
    def _sound_cb(self, widget):
        self.config.set_sound(widget.get_active())

    def sound(self):
        "Select sound quality"
        combo = gtk.combo_box_new_text()
        for text in self.config.get_sound_values():
            combo.append_text(text)
        combo.set_active(self.config.get_sound())
        combo.connect("changed", self._sound_cb)
        if self.to_horizontal_box:
            box = gtk.HBox(False, self.label_spacing/2)
        else:
            box = gtk.VBox()
        box.pack_start(gtk.Label("Sound:"), False, False)
        box.add(combo)
        return (box, False)

    # ------- screenshot control -----------
    def _screenshot_cb(self, widget):
        print "TODO: Support converting screenshot to PNG and giving its name?"
        self.hatari.trigger_shortcut("screenshot")

    def screenshot(self):
        "Take a screenshot"
        return (create_button("Screenshot", self._screenshot_cb), True)

    # ------- insert key special control -----------
    def _keypress_cb(self, widget, code):
        self.hatari.insert_event("keypress %s" % code)

    def _keyrelease_cb(self, widget, code):
        self.hatari.insert_event("keyrelease %s" % code)

    def _textinsert_cb(self, widget, text):
        HatariTextInsert(self.hatari, text)

    def create_key_control(self, name, textcode):
        "Simulate Atari key press/release and string inserting"
        if not textcode:
            return (None, None, None)
        widget = gtk.Button(name)
        try:
            # part after "=" converts to an int?
            code = int(textcode, 0)
            widget.connect("pressed", self._keypress_cb, code)
            widget.connect("released", self._keyrelease_cb, code)
            tip = "keycode: %d" % code
        except ValueError:
            # no, assume a string macro is wanted instead
            widget.connect("clicked", self._textinsert_cb, textcode)
            tip = "string '%s'" % textcode
        return (widget, tip, True)


# class for the main Hatari UI window, Hatari embedding, panels
# and control positioning
class HatariUI():
    def __init__(self):
        self.panel_controls = []
        self.panel_names = []
        self.panels = []
        # no controls set yet
        self.controls_left = None
        self.controls_right = None
        self.controls_top = None
        self.controls_bottom = None
        self.control_spacing = 4 # pixels
        self.controls = HatariControls()
        # other widgets
        self.tooltips = None
        self.mainwin = None

    # ----- control types ---------
    def set_controls(self, control_str, place):
        "set_controls(controls,place) -> error string, None if all OK"
        controls = control_str.split(",")
        for control in controls:
            if control in self.controls.all:
                # regular control
                if control == "close" and place != "panel":
                    return "close button can be only in a panel"
                continue
            if control in ("|", ">") or control in self.panel_names:
                # divider/line break or special panel control
                continue
            if control.find("=") >= 0:
                # special keycode/string control
                continue
            return "unrecognized control '%s'" % control

        if place == "left":
            self.controls_left = controls
        elif place == "right":
            self.controls_right = controls
        elif place == "top":
            self.controls_top = controls
        elif place == "bottom":
            self.controls_bottom = controls
        elif place == "panel":
            self.panel_controls.append(controls)
        else:
            return "unknown controls position '%s'" % place
        return None

    def add_panel(self, spec):
        "add_panel(panel_specification) -> error string, None if all is OK"
        error = None
        offset = spec.find(",")
        if offset <= 0:
            error = "invalid panel specification '%s'" % spec
        else:
            error = self.set_controls(spec[offset+1:], "panel")
            if not error:
                self.panel_names.append(spec[:offset])
                self.panels.append(None)
        return error

    def list_all_controls(self):
        "list_all_controls() -> list of (control, description) tuples"
        # generate the list from class internal documentation
        yield ("|", "Separator between controls")
        yield (">", "Line break between controls in panels")
        for methodname in self.controls.all:
            yield (methodname, HatariControls.__dict__[methodname].__doc__)
        yield ("<panel name>", self.add_panel_button.__doc__)
        yield ("<name>=<string/code>", "Insert string or single key <code>")

    def set_control_spacing(self, arg):
        "set_control_spacing(spacing) -> error string, None if given int-as-string OK"
        try:
            self.control_spacing = int(arg, 0)
            return None
        except ValueError:
            return "argument '%s' is not a number" % arg


    # ------- panel special control -----------
    def _panel_cb(self, widget, idx):
        if not self.panels[idx]:
            window = gtk.Window(gtk.WINDOW_TOPLEVEL)
            window.set_transient_for(self.mainwin)
            window.set_icon_from_file(UInfo.icon)
            window.set_title(self.panel_names[idx])
            if "close" in self.panel_controls[idx]:
                window.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_DIALOG)
            window.add(self.get_control_box(self.panel_controls[idx], True))
            window.connect("delete_event", window_hide_cb)
            self.panels[idx] = window
        self.panels[idx].show_all()
        self.panels[idx].deiconify()

    def add_panel_button(self, name):
        "Button for the specified panel window"
        index = self.panel_names.index(name)
        button = create_button(name, self._panel_cb, index)
        return (button, name, True)

    # ------- control widget box -----------
    def add_tooltip(self, widget, text):
        # get the first non-label child of a box container
        while widget.__gtype__ in (gtk.HBox.__gtype__, gtk.VBox.__gtype__):
            children = widget.get_children()
            if not children:
                break
            for child in children:
                if child.__gtype__ not in (gtk.Label.__gtype__,):
                    break
            widget = child
        #print widget.__gtype__, "tooltip:", text
        self.tooltips.set_tip(widget, text)
        
    def get_control_box(self, controls, horizontal):
        "return Gtk Box container with the specified control widgets or None for no controls"
        if not controls:
            return None
        if ">" in controls:
            if horizontal:
                box = gtk.VBox(False, self.control_spacing)
            else:
                box = gtk.BBox(False, self.control_spacing)
            while ">" in controls:
                linebreak = controls.index(">")
                box.add(self.get_control_box(controls[:linebreak], horizontal))
                controls = controls[linebreak+1:]
            if controls:
                box.add(self.get_control_box(controls, horizontal))
            return box

        self.controls.set_box_horizontal(horizontal)
        if horizontal:
            box = gtk.HBox(False, self.control_spacing)
        else:
            box = gtk.VBox(False, self.control_spacing)
        self.tooltips = gtk.Tooltips()
        for control in controls:
            offset = control.find("=")
            if offset >= 0:
                # handle "<name>=<keycode>" control specification
                name = control[:offset]
                text = control[offset+1:]
                (widget, tip, expand) = self.controls.create_key_control(name, text)
                self.tooltips.set_tip(widget, "Insert " + tip)
            elif control == "|":
                if horizontal:
                    widget = gtk.VSeparator()
                else:
                    widget = gtk.HSeparator()
                expand = False
            elif control in self.panel_names:
                (widget, tip, expand) = self.add_panel_button(control)
                self.tooltips.set_tip(widget, tip)
            else:
                method = HatariControls.__dict__[control]
                (widget, expand) = method(self.controls)
                self.add_tooltip(widget, method.__doc__)
            if not widget:
                continue
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            box.pack_start(widget, expand, expand, 0)
        return box


    # ---------- create UI ----------------
    def create_ui(self, fullscreen, embed):
        "create_ui(fullscreen, embed), args are booleans"
        # just instantiate all UI windows/widgets...
        mainwin, hatariparent = self._create_mainwin(embed)
        if fullscreen:
            mainwin.fullscreen()
        self.controls.set_mainwin_hatariparent(mainwin, hatariparent)
        self.mainwin = mainwin
        mainwin.show_all()

    def _create_mainwin(self, embed):
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
            hbox.pack_start(left, False, True)
        if embed:
            parent = self._create_uisocket()
            # no resizing for the Hatari window
            hbox.pack_start(parent, False, False)
        else:
            parent = None
        if right:
            hbox.pack_start(right, False, True)
        # add vertical elements
        vbox = gtk.VBox()
        actions = UIActions()
        vbox.add(actions.get_menu())
        if top:
            vbox.pack_start(top, False, True)
        vbox.add(hbox)
        if bottom:
            vbox.pack_start(bottom, False, True)
        # put them to main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.set_title("%s %s" % (UInfo.name, UInfo.version))
        mainwin.set_icon_from_file(UInfo.icon)
        mainwin.add(vbox)
        return (mainwin, parent)
    
    def _create_uisocket(self):
        # add Hatari parent container
        socket = gtk.Socket()
        # without this, closing Hatari would remove the socket widget
        socket.connect("plug-removed", lambda obj: True)
        socket.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("black"))
        socket.set_events(gtk.gdk.ALL_EVENTS_MASK)
        socket.set_flags(gtk.CAN_FOCUS)
        return socket


def usage(app, msg=None):
    name = os.path.basename(sys.argv[0])
    uiname = "%s %s" % (UInfo.name, UInfo.version)
    print "\n%s" % uiname
    print "=" * len(uiname)
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
    print "\t-p, --panel <name>,<controls>"
    print "\t\t\t\tseparate window with given name and controls"
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
You can have as many panels as you wish.  For each panel you need to add
a control with the name of the panel (see "MyPanel" below).

For example:
\t%s --embed \\
\t-t "about,run,pause,quit" \\
\t-p "MyPanel,Macro=Test,Undo=97,Help=98,>,F1=59,F2=60,F3=61,F4=62,>,close" \\
\t-r "paste,debug,trace,setup,MyPanel" \\
\t-b "sound,|,fastforward,|,fullscreen"

if no options are given, the UI uses basic controls.
""" % name
    if msg:
        print "ERROR: %s\n" % msg
    sys.exit(1)


def main():
    app = HatariUI()
    try:
        longopts = ["embed", "fullscreen", "help",
            "left=", "right=", "top=", "bottom=", "panel=", "spacing="]
        opts, args = getopt.getopt(sys.argv[1:], "efhl:r:t:b:p:s:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(app, err)
    if args:
        usage(app, "arguments given although only options accepted")

    error = None
    embed = False
    fullscreen = False
    for opt, arg in opts:
        print opt, arg
        if opt in ("-e", "--embed"):
            embed = True
        elif opt in ("-f", "--fullscreen"):
            fullscreen = True
        elif opt in ("-h", "--help"):
            usage(app)
        elif opt in ("-l", "--left"):
            error = app.set_controls(arg, "left")
        elif opt in ("-r", "--right"):
            error = app.set_controls(arg, "right")
        elif opt in ("-t", "--top"):
            error = app.set_controls(arg, "top")
        elif opt in ("-b", "--bottom"):
            error = app.set_controls(arg, "bottom")
        elif opt in ("-p", "--panel"):
            error = app.add_panel(arg)
        elif opt in ("-s", "--spacing"):
            error = app.set_control_spacing(arg)
        else:
            assert False, "getopt returned unhandled option"
        if error:
            usage(app, error)

    info = UInfo()
    app.create_ui(fullscreen, embed)
    gtk.main()

if __name__ == "__main__":
    main()
