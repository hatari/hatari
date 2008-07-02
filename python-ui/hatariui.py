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
from uihelpers import UInfo, create_button, create_toggle, create_toolbutton
from dialogs import AboutDialog, InputDialog, KillDialog, QuitSaveDialog, \
     ResetDialog, SetupDialog, TraceDialog, PeripheralsDialog, ErrorDialog, \
     DisplayDialog


# helper functions to match callback args
def window_hide_cb(window, arg):
    window.hide()
    return True


class UIActions:
    def __init__(self):
        self.actions = gtk.ActionGroup("all")
        # name, icon ID, label, accel, tooltip, callback
        self.actions.add_toggle_actions((
        ("fast", None, "FastForward", None, "Whether to fast forward Hatari (needs fast machine)", None),
        ("full", None, "Fullscreen", None, "Toggle whether Hatari is fullscreen", None)
        ))
        self.actions.add_actions((
        # name, icon ID, label, accel, tooltip, callback
        ("about", None, "About", None, "Hatari UI information", None),
        ("shot", None, "Screenshot", None, "Take a screenshot", None),
        ("quit", None, "Quit", None, "Quit Hatari UI", None),
        
        ("run", None, "Run", None, "(Re-)run Hatari", None),
        ("pause", None, "Pause", None, "Pause Hatari to save battery", None),
        ("reset", None, "Reset...", None, "Warm or cold reset Hatari", None),
        
        ("sound", None, "Sound...", None, "Sound settings", None),
        ("display", None, "Display...", None, "Display settings", None),
        ("devices", None, "Devices...", None, "Floppy and joystick settings", None),
        ("machine", None, "Machine...", None, "Hatari st/e/tt/falcon configuration", None),
        
        ("debug", None, "Debugger...", None, "Activate Hatari debugger", None),
        ("trace", None, "Trace settings...", None, "Hatari tracing setup", None),
        
        # not in menu, just as buttons
        ("input", None, "Input...", None, "Simulate text input and mouse clicks", None),
        ("close", None, "Close", None, "Close button for panel windows", None)
        ))
        self.action_names = [x.get_name() for x in self.actions.list_actions()]
        self.panel_actions = []
        self.panel_names = []
        self.panels = []
        # no actions set yet
        self.toolbars = {}

    # ----- toolbar / panel additions ---------
    def set_actions(self, action_str, place):
        "set_actions(actions,place) -> error string, None if all OK"
        actions = action_str.split(",")
        for action in actions:
            if action in self.action_names:
                # regular action
                if action == "close" and place != "panel":
                    return "close button can be only in a panel"
                continue
            if action in ("|", ">") or action in self.panel_names:
                # divider/line break or special panel action
                continue
            if action.find("=") >= 0:
                # special keycode/string action
                continue
            return "unrecognized action '%s'" % action

        if place in ("left", "right", "top", "bottom"):
            self.toolbars[place] = actions
        elif place == "panel":
            self.panel_actions.append(actions)
        else:
            return "unknown actions position '%s'" % place
        return None

    def add_panel(self, spec):
        "add_panel(panel_specification) -> error string, None if all is OK"
        error = None
        offset = spec.find(",")
        if offset <= 0:
            error = "invalid panel specification '%s'" % spec
        else:
            error = self.set_actions(spec[offset+1:], "panel")
            if not error:
                self.panel_names.append(spec[:offset])
                self.panels.append(None)
        return error

    def list_actions(self):
        yield ("|", "Separator between actions")
        yield (">", "Line break between actions in panel windows")
        # generate the list from action information
        for act in self.actions.list_actions():
            yield(act.get_name(), act.get_property("tooltip"))
        yield ("<panel name>", self.add_panel_button.__doc__)
        yield ("<name>=<string/code>", "Synthetize string or single key <code>")

    # ------- panel special action -----------
    def _panel_cb(self, widget, idx):
        if not self.panels[idx]:
            window = gtk.Window(gtk.WINDOW_TOPLEVEL)
            window.set_transient_for(self.mainwin)
            window.set_icon_from_file(UInfo.icon)
            window.set_title(self.panel_names[idx])
            if "close" in self.panel_actions[idx]:
                window.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_DIALOG)
            window.add(self.get_action_box(self.panel_actions[idx], True))
            window.connect("delete_event", window_hide_cb)
            self.panels[idx] = window
        self.panels[idx].show_all()
        self.panels[idx].deiconify()

    def add_panel_button(self, name):
        "Button for the specified panel window"
        index = self.panel_names.index(name)
        # TODO: icon for the panel window toolbar button
        button = create_toolbutton(None, name, self._panel_cb, index)
        return (button, name)

    # ------- key special action -----------
    def create_key_control(self, name, textcode):
        "Simulate Atari key press/release and string inserting"
        if not textcode:
            return (None, None, None)
        widget = gtk.Button(name)
        try:
            # part after "=" converts to an int?
            code = int(textcode, 0)
            widget.connect("pressed", self.callbacks.keypress, code)
            widget.connect("released", self.callbacks.keyrelease, code)
            tip = "keycode: %d" % code
        except ValueError:
            # no, assume a string macro is wanted instead
            widget.connect("clicked", self.callbacks_textinsert, textcode)
            tip = "string '%s'" % textcode
        return (widget, tip)

    def get_container(self, side, orientation):
        "return Gtk container with the specified actions or None for no actions"
        if side not in self.toolbars:
            return None
        actions = self.toolbars[side]
        if not actions:
            return None
        
        if ">" in actions:
            if orientation == gtk.ORIENTATION_HORIZONTAL:
                box = gtk.VBox(False, self.action_spacing)
            else:
                box = gtk.BBox(False, self.action_spacing)
            while ">" in actions:
                linebreak = actions.index(">")
                box.add(self.get_toolbar(actions[:linebreak], orientation))
                actions = actions[linebreak+1:]
            if actions:
                box.add(self.get_toolbar(actions, orientation))
            return box

        bar = gtk.Toolbar()
        bar.set_orientation(orientation)        
        tooltips = gtk.Tooltips()
        
        for action in actions:
            offset = action.find("=")
            if offset >= 0:
                # handle "<name>=<keycode>" action specification
                name = action[:offset]
                text = action[offset+1:]
                (widget, tip) = self.create_key_control(name, text)
                tooltips.set_tip(widget, "Insert " + tip)
            elif action == "|":
                if horizontal:
                    widget = gtk.VSeparator()
                else:
                    widget = gtk.HSeparator()
            elif action in self.panel_names:
                (widget, tip) = self.add_panel_button(action)
                tooltips.set_tip(widget, tip)
            else:
                print action
                widget = self.actions.get_action(action).create_tool_item()
            if not widget:
                continue
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            bar.insert(widget, -1)
        return bar

    # ------------- handling menu -------------
    def activate(self, widget):
        print "activated:", widget.get_name()
        
    def get_menu(self):
        allmenus = (
        ("File", ("about", None, "shot", None, "quit")),
        ("Emulation", ("run", "pause", None, "fast", None, "full", None, "reset")),
        ("Setup", ("sound", "display", "devices", "machine")),
        ("Debug", ("debug", "trace"))
        )
        bar = gtk.MenuBar()

        for title, barmenu in allmenus:
            submenu = gtk.Menu()
            for name in barmenu:
                if name:
                    action = self.actions.get_action(name)
                    # dummy action
                    action.connect("activate", self.activate)
                    item = action.create_menu_item()
                else:
                    item = gtk.SeparatorMenuItem()
                submenu.add(item)
            baritem = gtk.MenuItem(title, False)
            baritem.set_submenu(submenu)
            bar.add(baritem)

        print "TODO: add panels menu"
        return bar


# class for all the controls that the different UI panels and windows can have
class HatariControls():
    # these are the names of methods returning (control widget, expand flag) tuples
    # which also give description of the co. control in their __doc__ attribute
    #
    # (in a more OO application all these widgets would be separate classes
    # inheriting a common interface class, but that would be an overkill)
    all = [
        "about", "run", "pause", "reset", "machine", "quit",
        "full", "fast", "display", "sound", "shot",
        "input", "devices", "debug", "trace", "close"
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
        self.devicesdialog = None
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

    # ------- input control -----------
    def _input_cb(self, widget):
        if not self.inputdialog:
            self.inputdialog = InputDialog(self.mainwin)
        self.inputdialog.run(self.hatari)

    def input(self):
        "Insert text/mouse clicks to Hatari"
        return (create_button("Input", self._input_cb), True)

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

    # ------- devices control -----------
    def _devices_cb(self, widget):
        if not self.devicesdialog:
            self.devicesdialog = DevicesDialog(self.mainwin)
        self.devicesdialog.run(self.config)

    def devices(self):
        "Dialog for Hatari devices settings"
        return (create_button("Devices", self._devices_cb), True)

    # ------- display control -----------
    def _display_cb(self, widget):
        if not self.displaydialog:
            self.displaydialog = DisplayDialog(self.mainwin)
        self.displaydialog.run(self.config)

    def display(self):
        "Dialog for Hatari display settings"
        return (create_button("Display", self._display_cb), True)

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


# class for the main Hatari UI window, Hatari embedding, panels
# and control positioning
class HatariUI():
    def __init__(self):
        # other widgets
        self.tooltips = None
        self.mainwin = None
        self.controls = HatariControls()

    # ---------- create UI ----------------
    def create_ui(self, actions, fullscreen, embed):
        "create_ui(fullscreen, embed), args are booleans"
        # just instantiate all UI windows/widgets...
        mainwin, hatariparent = self._create_mainwin(actions, embed)
        if fullscreen:
            mainwin.fullscreen()
        self.controls.set_mainwin_hatariparent(mainwin, hatariparent)
        self.mainwin = mainwin
        mainwin.show_all()

    def _create_mainwin(self, actions, embed):
        # create toolbars
        orient = gtk.ORIENTATION_VERTICAL
        left   = actions.get_container("left", orient)
        right  = actions.get_container("right", orient)
        orient = gtk.ORIENTATION_HORIZONTAL
        top    = actions.get_container("top", orient)
        bottom = actions.get_container("bottom", orient)
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


def usage(actions, msg=None):
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
    print "\t-p, --panel <name>,<controls>"
    print "\t\t\t\tseparate window with given name and controls"
    print "\nAvailable controls:"
    for action, description in actions.list_actions():
        size = len(action)
        if size < 8:
            tabs = "\t\t"
        elif size < 16:
            tabs = "\t"
        else:
            tabs = "\n\t\t\t"
        print "\t%s%s%s" % (action, tabs, description)
    print """
You can have as many panels as you wish.  For each panel you need to add
a action with the name of the panel (see "MyPanel" below).

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
    actions = UIActions()
    try:
        longopts = ["embed", "fullscreen", "help",
            "left=", "right=", "top=", "bottom=", "panel=", "spacing="]
        opts, args = getopt.getopt(sys.argv[1:], "efhl:r:t:b:p:s:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(actions, err)
    if args:
        usage(actions, "arguments given although only options accepted")

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
            usage(actions)
        elif opt in ("-l", "--left"):
            error = actions.set_actions(arg, "left")
        elif opt in ("-r", "--right"):
            error = actions.set_actions(arg, "right")
        elif opt in ("-t", "--top"):
            error = actions.set_actions(arg, "top")
        elif opt in ("-b", "--bottom"):
            error = actions.set_actions(arg, "bottom")
        elif opt in ("-p", "--panel"):
            error = actions.add_panel(arg)
        else:
            assert False, "getopt returned unhandled option"
        if error:
            usage(actions, error)

    info = UInfo()
    HatariUI().create_ui(actions, fullscreen, embed)
    gtk.main()

if __name__ == "__main__":
    main()
