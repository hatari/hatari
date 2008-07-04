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
from uihelpers import UInfo, create_button, create_toggle
from dialogs import AboutDialog, InputDialog, KillDialog, QuitSaveDialog, \
     ResetDialog, SetupDialog, TraceDialog, PeripheralsDialog, ErrorDialog, \
     DisplayDialog


# helper functions to match callback args
def window_hide_cb(window, arg):
    window.hide()
    return True


# ---------------------------------------------------------------
# Class with Hatari and configuration instances which methods are
# called to change those (with additional dialogs or directly).
# Owns the application window and socket widget embedding Hatari.
class UICallbacks():
    def __init__(self):
        # Hatari and configuration
        self.hatari = Hatari()
        self.config = HatariConfigMapping(self.hatari)
        if not self.config.is_loaded():
            ErrorDialog(None).run("Loading Hatari configuration failed.\nMake sure you've saved one!")
            sys.exit(1)
        
        # windows are created when needed
        self.mainwin = None
        self.hatariwin = None
        self.debugui = None
        self.panels = {}
        # dialogs are created when needed
        self.aboutdialog = None
        self.resetdialog = None
        self.inputdialog = None
        self.devicesdialog = None
        self.displaydialog = None
        self.pastedialog = None
        self.quitdialog = None
        self.setupdialog = None
        self.tracedialog = None
        self.killdialog = None

        # TODO: Hatari UI configuration settings save/load
        self.tracepoints = None
        # used by run()
        self.io_id = None

    # ---------- create UI ----------------
    def create_ui(self, menu, toolbars, fullscreen, embed):
        "create_ui(menu, toolbars, fullscreen, embed)"
        # add horizontal elements
        hbox = gtk.HBox()
        if toolbars["left"]:
            #hbox.add(toolbars["left"])
            hbox.pack_start(toolbars["left"], False, True)
        if embed:
            self._add_uisocket(hbox)
        if toolbars["right"]:
            #hbox.add(toolbars["right"])
            hbox.pack_start(toolbars["right"], False, True)
        # add vertical elements
        vbox = gtk.VBox()
        if menu:
            vbox.add(menu)
        if toolbars["top"]:
            #vbox.add(toolbars["top"])
            vbox.pack_start(toolbars["top"], False, True)
        vbox.add(hbox)
        if toolbars["bottom"]:
            #vbox.add(toolbars["bottom"])
            vbox.pack_start(toolbars["bottom"], False, True)
        # put them to main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.set_title("%s %s" % (UInfo.name, UInfo.version))
        mainwin.set_icon_from_file(UInfo.icon)
        mainwin.add(vbox)
        if fullscreen:
            mainwin.fullscreen()
        mainwin.show_all()
        # for run and quit callbacks
        self.killdialog = KillDialog(mainwin)
        mainwin.connect("delete_event", self.quit)
        self.mainwin = mainwin
    
    def _add_uisocket(self, box):
        # add Hatari parent container to given box
        socket = gtk.Socket()
        # without this, closing Hatari would remove the socket widget
        socket.connect("plug-removed", lambda obj: True)
        socket.modify_bg(gtk.STATE_NORMAL, gtk.gdk.color_parse("black"))
        socket.set_events(gtk.gdk.ALL_EVENTS_MASK)
        socket.set_flags(gtk.CAN_FOCUS)
        # set initial embedded hatari size
        width, height = self.config.get_window_size()
        socket.set_size_request(width, height)
        # no resizing for the Hatari window
        box.pack_start(socket, False, False)
        self.hatariwin = socket

    # ------- run callback -----------
    def _socket_cb(self, fd, event):
        if event != gobject.IO_IN:
            # hatari process died, make sure Hatari instance notices
            self.hatari.kill()
            return False
        width, height = self.hatari.get_embed_info()
        print "New size = %d x %d" % (width, height)
        oldwidth, oldheight = self.hatariwin.get_size_request()
        self.hatariwin.set_size_request(width, height)
        if width < oldwidth or height < oldheight:
            # force also mainwin smaller (it automatically grows)
            self.mainwin.resize(width, height)
        return True

    def run(self, widget=None):
        if self.killdialog.run(self.hatari):
            return
        if self.io_id:
            gobject.source_remove(self.io_id)
        if self.hatariwin:
            size = self.hatariwin.window.get_size()
            self.hatari.run(None, self.hatariwin.window)
            # get notifications of Hatari window size changes
            self.hatari.enable_embed_info()
            socket = self.hatari.get_control_socket().fileno()
            events = gobject.IO_IN | gobject.IO_HUP | gobject.IO_ERR
            self.io_id = gobject.io_add_watch(socket, events, self._socket_cb)
        else:
            self.hatari.run()

    # ------- about callback -----------
    def about(self, widget):
        if not self.aboutdialog:
            self.aboutdialog = AboutDialog(self.mainwin)
        self.aboutdialog.run()

    # ------- input callback -----------
    def inputs(self, widget):
        if not self.inputdialog:
            self.inputdialog = InputDialog(self.mainwin)
        self.inputdialog.run(self.hatari)

    # ------- pause callback -----------
    def pause(self, widget):
        if widget.get_active():
            self.hatari.pause()
        else:
            self.hatari.unpause()

    # ------- reset callback -----------
    def reset(self, widget):
        if not self.resetdialog:
            self.resetdialog = ResetDialog(self.mainwin)
        self.resetdialog.run(self.hatari)

    # ------- setup callback -----------
    def setup(self, widget):
        if not self.setupdialog:
            self.setupdialog = SetupDialog(self.mainwin)
        if self.setupdialog.run(self.config):
            self.hatari.trigger_shortcut("warmreset")

    # ------- quit callback -----------
    def quit(self, widget, arg = None):
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

    # ------- devices callback -----------
    def devices(self, widget):
        if not self.devicesdialog:
            self.devicesdialog = PeripheralsDialog(self.mainwin)
        self.devicesdialog.run(self.config)

    # ------- display callback -----------
    def display(self, widget):
        if not self.displaydialog:
            self.displaydialog = DisplayDialog(self.mainwin)
        self.displaydialog.run(self.config)

    # ------- debug callback -----------
    def debugger(self, widget):
        if not self.debugui:
            self.debugui = HatariDebugUI(self.hatari)
        self.debugui.show()

    # ------- trace callback -----------
    def trace(self, widget):
        if not self.tracedialog:
            self.tracedialog = TraceDialog(self.mainwin)
        self.tracepoints = self.tracedialog.run(self.hatari, self.tracepoints)

    # ------- fast forward callback -----------
    def fastforward(self, widget):
        self.config.set_fastforward(widget.get_active())

    def _fastforward(self):
        # TODO: where to setup these?
        "Whether to fast forward Hatari (needs fast machine)"
        widget = gtk.CheckButton("FastForward")
        widget.set_active(self.config.get_fastforward())
        widget.connect("toggled", self._fastforward_cb)
        return (widget, False)

    # ------- fullscreen callback -----------
    def fullscreen(self, widget):
        self.config.set_fullscreen(widget.get_active())

    def _fullscreen(self):
        # TODO: where to setup these?
        "Hatari window fullscreen toggle"
        widget = gtk.CheckButton("fullscreen")
        widget.set_active(self.config.get_fullscreen())
        widget.connect("toggled", self._fullscreen_cb)
        return (widget, False)

    # ------- sound callback -----------
    def sound(self, widget):
        self.config.set_sound(widget.get_active())

    def _sound(self):
        #TODO: move to a dialog or menu
        "Select sound quality"
        combo = gtk.combo_box_new_text()
        for text in self.config.get_sound_values():
            combo.append_text(text)
        combo.set_active(self.config.get_sound())
        combo.connect("changed", self._sound_cb)
        box = gtk.HBox()
        box.pack_start(gtk.Label("Sound:"), False, False)
        box.add(combo)
        return (box, False)

    # ------- screenshot callback -----------
    def screenshot(self, widget):
        print "TODO: Support converting screenshot to PNG and giving its name?"
        self.hatari.trigger_shortcut("screenshot")

    # ------- insert key special callback -----------
    def keypress(self, widget, code):
        self.hatari.insert_event("keypress %s" % code)
        self.hatari.insert_event("keyrelease %s" % code)

    def textinsert(self, widget, text):
        HatariTextInsert(self.hatari, text)

    # ------- panel callback -----------
    def panel(self, widget, info):
        title, content = info
        if title not in self.panels:
            window = gtk.Window(gtk.WINDOW_TOPLEVEL)
            window.set_transient_for(self.mainwin)
            window.set_icon_from_file(UInfo.icon)
            window.set_title(title)
            window.add(content)
            window.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_DIALOG)
            window.connect("delete_event", window_hide_cb)
            self.panels[title] = window
        else:
            window = self.panels(title)
        window.show_all()
        window.deiconify()



# ---------------------------------------------------------------
# class for creating menus, toolbars and panels
# and managing actions bound to them
class UIActions:
    def __init__(self):
        cb = UICallbacks()
        # TODO: add icons
        self.actions = gtk.ActionGroup("all")
        # name, icon ID, label, accel, tooltip, callback
        self.actions.add_toggle_actions((
        ("pause", gtk.STOCK_MEDIA_PAUSE, "Pause", None, "Pause Hatari to save battery", cb.pause),
        ("fast", gtk.STOCK_MEDIA_FORWARD, "FastForward", None, "Whether to fast forward Hatari (needs fast machine)", cb.fastforward),
        # TODO: how to know when Hatari gets back from fullscreen?
        ("full", gtk.STOCK_FULLSCREEN, "Fullscreen", None, "Toggle whether Hatari is fullscreen", cb.fullscreen)
        ))
        self.actions.add_actions((
        # name, icon ID, label, accel, tooltip, callback
        ("about", gtk.STOCK_INFO, "About", None, "Hatari UI information", cb.about),
        ("shot", gtk.STOCK_MEDIA_RECORD, "Screenshot", None, "Take a screenshot", cb.screenshot),
        ("quit", gtk.STOCK_QUIT, "Quit", None, "Quit Hatari UI", cb.quit),
        
        ("run", gtk.STOCK_MEDIA_PLAY, "Run", None, "(Re-)run Hatari", cb.run),
        ("input", gtk.STOCK_SPELL_CHECK, "Inputs...", None, "Simulate text input and mouse clicks", cb.inputs),
        ("reset", gtk.STOCK_REFRESH, "Reset...", None, "Warm or cold reset Hatari", cb.reset),
        
        ("sound", gtk.STOCK_YES, "Sound...", None, "Sound settings", cb.sound),
        ("display", gtk.STOCK_PROPERTIES, "Display...", None, "Display settings", cb.display),
        ("devices", gtk.STOCK_FLOPPY, "Peripherals...", None, "Floppy and joystick settings", cb.devices),
        ("machine", gtk.STOCK_PREFERENCES, "Machine...", None, "Hatari st/e/tt/falcon configuration", cb.setup),
        
        ("debug", gtk.STOCK_FIND, "Debugger...", None, "Activate Hatari debugger", cb.debugger),
        ("trace", gtk.STOCK_EXECUTE, "Trace settings...", None, "Hatari tracing setup", cb.trace)
        ))
        self.action_names = [x.get_name() for x in self.actions.list_actions()]
        self.callbacks = cb

        self.panel_actions = []
        self.panel_names = []
        # no actions set yet
        self.toolbars = {}

    # ----- toolbar / panel additions ---------
    def set_actions(self, action_str, place):
        "set_actions(actions,place) -> error string, None if all OK"
        actions = action_str.split(",")
        for action in actions:
            if action in self.action_names:
                # regular action
                continue
            if action in ("|", ">") or action in self.panel_names:
                # divider/line break or user specified panel
                continue
            if action == "close":
                if place != "panel":
                    return "close button can be only in a panel"
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
        return error

    def list_actions(self):
        yield ("|", "Separator between controls")
        yield (">", "Start next toolbar in panel windows")
        # generate the list from action information
        for act in self.actions.list_actions():
            yield(act.get_name(), act.get_property("tooltip"))
        yield ("<panel name>", "Button for the specified panel window")
        yield ("<name>=<string/code>", "Synthetize string or single key <code>")

    # ------- panel special actions -----------
    def _close_cb(self, widget):
        widget.get_toplevel().hide()

    def _create_panel_button(self, name):
        index = self.panel_names.index(name)
        controls = self._get_container(self.panel_actions[index], True)
        title = self.panel_names[index]
        widget = gtk.ToolButton(gtk.STOCK_ADD)
        widget.connect("clicked", self.callbacks.panel, (title, controls))
        widget.set_label(name)
        return (widget, name)

    # ------- key special action -----------
    def _create_key_control(self, name, textcode):
        "Simulate Atari key press/release and string inserting"
        if not textcode:
            return (None, None)
        # TODO: icon for the key button
        widget = gtk.ToolButton(gtk.STOCK_PASTE)
        widget.set_label(name)
        try:
            # part after "=" converts to an int?
            code = int(textcode, 0)
            widget.connect("clicked", self.callbacks.keypress, code)
            tip = "keycode: %d" % code
        except ValueError:
            # no, assume a string macro is wanted instead
            widget.connect("clicked", self.callbacks.textinsert, textcode)
            tip = "string '%s'" % textcode
        return (widget, tip)

    def _get_container(self, actions, horiz):
        "return Gtk container with the specified actions or None for no actions"
        if not actions:
            return None
        
        if ">" in actions:
            if horiz:
                box = gtk.VBox()
            else:
                box = gtk.HBox()
            while ">" in actions:
                linebreak = actions.index(">")
                box.add(self._get_container(actions[:linebreak], horiz))
                actions = actions[linebreak+1:]
            if actions:
                box.add(self._get_container(actions, horiz))
            return box

        bar = gtk.Toolbar()
        if horiz:
            bar.set_orientation(gtk.ORIENTATION_HORIZONTAL)
        else:
            bar.set_orientation(gtk.ORIENTATION_VERTICAL)
        bar.set_style(gtk.TOOLBAR_BOTH)
        # important, without this Hatari doesn't receive key events!
        bar.unset_flags(gtk.CAN_FOCUS)
        bar.set_tooltips(True)
        tooltips = gtk.Tooltips()
        
        for action in actions:
            offset = action.find("=")
            if offset >= 0:
                # handle "<name>=<keycode>" action specification
                name = action[:offset]
                text = action[offset+1:]
                (widget, tip) = self._create_key_control(name, text)
                widget.set_tooltip(tooltips, "Insert " + tip)
            elif action == "|":
                widget = gtk.SeparatorToolItem()
            elif action == "close":
                widget = gtk.ToolButton(gtk.STOCK_CLOSE)
                widget.connect("clicked", self._close_cb)
            elif action in self.panel_names:
                (widget, tip) = self._create_panel_button(action)
                widget.set_tooltip(tooltips, tip)
            else:
                widget = self.actions.get_action(action).create_tool_item()
            if not widget:
                continue
            if action != "|":
                widget.set_expand(True)
            # important, without this Hatari doesn't receive key events!
            widget.unset_flags(gtk.CAN_FOCUS)
            bar.insert(widget, -1)
        return bar

    # ------------- handling menu -------------
    def _get_menu(self):
        allmenus = (
        ("File", ("about", None, "shot", None, "quit")),
        ("Emulation", ("run", "pause", None, "fast", None, "full", None, "input", None, "reset")),
        ("Setup", ("sound", "display", "devices", "machine")),
        ("Debug", ("debug", "trace"))
        )
        bar = gtk.MenuBar()

        for title, barmenu in allmenus:
            submenu = gtk.Menu()
            for name in barmenu:
                if name:
                    action = self.actions.get_action(name)
                    item = action.create_menu_item()
                else:
                    item = gtk.SeparatorMenuItem()
                submenu.add(item)
            baritem = gtk.MenuItem(title, False)
            baritem.set_submenu(submenu)
            bar.add(baritem)

        print "TODO: add panels menu"
        return bar

    # ------------- run the whole UI -------------
    def run(self, havemenu, fullscreen, embed):
        # create menu?
        if havemenu:
            menu = self._get_menu()
        else:
            menu = None

        # create toolbars
        toolbars = { "left":None, "right":None, "top":None, "bottom":None}
        for side in ("left", "right"):
            if side in self.toolbars:
                toolbars[side] = self._get_container(self.toolbars[side], False)
        for side in ("top", "bottom"):
            if side in self.toolbars:
                toolbars[side] = self._get_container(self.toolbars[side], True)

        self.callbacks.create_ui(menu, toolbars, fullscreen, embed)

        # ugly, Hatari socket window ID can be gotten only
        # after Socket window is realized by gtk_main()
        gobject.idle_add(self.callbacks.run)
        gtk.main()


# ------------- usage / argument handling --------------
def usage(actions, msg=None):
    name = os.path.basename(sys.argv[0])
    uiname = "%s %s" % (UInfo.name, UInfo.version)
    print "\n%s" % uiname
    print "=" * len(uiname)
    print "\nUsage: %s [options]" % name
    print "\nOptions:"
    print "\t-h, --help\t\tthis help"
    print "\t-n, --nomenu\t\tomit menus"
    print "\t-e, --embed\t\tembed Hatari window in middle of controls"
    print "\t-f, --fullscreen\tstart in fullscreen"
    print "\t-l, --left <controls>\ttoolbar at left"
    print "\t-r, --right <controls>\ttoolbar at right"
    print "\t-t, --top <controls>\ttoolbar at top"
    print "\t-b, --bottom <controls>\ttoolbar at bottom"
    print "\t-p, --panel <name>,<controls>"
    print "\t\t\t\tseparate window with given name and controls"
    print "\nAvailable (toolbar) controls:"
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
    info = UInfo()
    actions = UIActions()
    try:
        longopts = ["embed", "fullscreen", "nomenu", "help",
            "left=", "right=", "top=", "bottom=", "panel="]
        opts, args = getopt.getopt(sys.argv[1:], "efnhl:r:t:b:p:", longopts)
        del longopts
    except getopt.GetoptError, err:
        usage(actions, err)
    if args:
        usage(actions, "arguments given although only options accepted")

    menu = True
    embed = False
    fullscreen = False

    error = None
    for opt, arg in opts:
        print opt, arg
        if opt in ("-e", "--embed"):
            embed = True
        elif opt in ("-f", "--fullscreen"):
            fullscreen = True
        elif opt in ("-n", "--nomenu"):
            menu = False
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

    actions.run(menu, fullscreen, embed)


if __name__ == "__main__":
    main()
