#!/usr/bin/env python
#
# A PyGtk UI for Hatari that can embed the Hatari emulator window.
#
# Requires PyGtk (python-gtk2) package and its dependencies to be present.
#
# Copyright (C) 2008-2011 by Eero Tamminen <eerot at berlios>
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
from uihelpers import UInfo, UIHelp, create_button, create_toolbutton, \
     create_toggle, HatariTextInsert, get_open_filename, get_save_filename
from dialogs import AboutDialog, TodoDialog, NoteDialog, ErrorDialog, \
     InputDialog, KillDialog, QuitSaveDialog, ResetDialog, TraceDialog, \
     FloppyDialog, HardDiskDialog, DisplayDialog, JoystickDialog, \
     MachineDialog, PeripheralDialog, PathDialog, SoundDialog


# helper functions to match callback args
def window_hide_cb(window, arg):
    window.hide()
    return True


# ---------------------------------------------------------------
# Class with Hatari and configuration instances which methods are
# called to change those (with additional dialogs or directly).
# Owns the application window and socket widget embedding Hatari.
class UICallbacks:
    tmpconfpath = os.path.expanduser("~/.hatari/.tmp.cfg")
    def __init__(self):
        # Hatari and configuration
        self.hatari = Hatari()
        error = self.hatari.is_compatible()
        if error:
            ErrorDialog(None).run(error)
            sys.exit(-1)
            
        self.config = HatariConfigMapping(self.hatari)
        try:
            self.config.validate()
        except (KeyError, AttributeError):
            NoteDialog(None).run("Loading Hatari configuration failed!\nRetrying after saving Hatari configuration.")
            self.hatari.save_config()
            self.config = HatariConfigMapping(self.hatari)
            self.config.validate()
        
        # windows are created when needed
        self.mainwin = None
        self.hatariwin = None
        self.debugui = None
        self.panels = {}

        # dialogs are created when needed
        self.aboutdialog = None
        self.inputdialog = None
        self.tracedialog = None
        self.resetdialog = None
        self.quitdialog = None
        self.killdialog = None

        self.floppydialog = None
        self.harddiskdialog = None
        self.displaydialog = None
        self.joystickdialog = None
        self.machinedialog = None
        self.peripheraldialog = None
        self.sounddialog = None
        self.pathdialog = None
        
        # used by run()
        self.memstate = None
        self.floppy = None
        self.io_id = None

        # TODO: Hatari UI own configuration settings save/load
        self.tracepoints = None

    def _reset_config_dialogs(self):
        self.floppydialog = None
        self.harddiskdialog = None
        self.displaydialog = None
        self.joystickdialog = None
        self.machinedialog = None
        self.peripheraldialog = None
        self.sounddialog = None
        self.pathdialog = None
        
    # ---------- create UI ----------------
    def create_ui(self, accelgroup, menu, toolbars, fullscreen, embed):
        "create_ui(menu, toolbars, fullscreen, embed)"
        # add horizontal elements
        hbox = gtk.HBox()
        if toolbars["left"]:
            hbox.pack_start(toolbars["left"], False, True)
        if embed:
            self._add_uisocket(hbox)
        if toolbars["right"]:
            hbox.pack_start(toolbars["right"], False, True)
        # add vertical elements
        vbox = gtk.VBox()
        if menu:
            vbox.add(menu)
        if toolbars["top"]:
            vbox.pack_start(toolbars["top"], False, True)
        vbox.add(hbox)
        if toolbars["bottom"]:
            vbox.pack_start(toolbars["bottom"], False, True)
        # put them to main window
        mainwin = gtk.Window(gtk.WINDOW_TOPLEVEL)
        mainwin.set_title("%s %s" % (UInfo.name, UInfo.version))
        mainwin.set_icon_from_file(UInfo.icon)
        if accelgroup:
            mainwin.add_accel_group(accelgroup)
        if fullscreen:
            mainwin.fullscreen()
        mainwin.add(vbox)
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
        print("New size = %d x %d" % (width, height))
        oldwidth, oldheight = self.hatariwin.get_size_request()
        self.hatariwin.set_size_request(width, height)
        if width < oldwidth or height < oldheight:
            # force also mainwin smaller (it automatically grows)
            self.mainwin.resize(width, height)
        return True
    
    def run(self, widget = None):
        if not self.killdialog.run(self.hatari):
            return
        if self.io_id:
            gobject.source_remove(self.io_id)
        args = ["--configfile"]
        # whether to use Hatari config or unsaved Hatari UI config?
        if self.config.is_changed():
            args += [self.config.save_tmp(self.tmpconfpath)]
        else:
            args += [self.config.get_path()]
        if self.memstate:
            args += self.memstate
        # only way to change boot order is to specify disk on command line
        if self.floppy:
            args += self.floppy
        if self.hatariwin:
            size = self.hatariwin.window.get_size()
            self.hatari.run(args, self.hatariwin.window)
            # get notifications of Hatari window size changes
            self.hatari.enable_embed_info()
            socket = self.hatari.get_control_socket().fileno()
            events = gobject.IO_IN | gobject.IO_HUP | gobject.IO_ERR
            self.io_id = gobject.io_add_watch(socket, events, self._socket_cb)
            # all keyboard events should go to Hatari window
            self.hatariwin.grab_focus()
        else:
            self.hatari.run(args)

    def set_floppy(self, floppy):
        self.floppy = floppy

    # ------- quit callback -----------
    def quit(self, widget, arg = None):
        # due to Gtk API, needs to return True when *not* quitting
        if not self.killdialog.run(self.hatari):
            return True
        if self.io_id:
            gobject.source_remove(self.io_id)
        if self.config.is_changed():
            if not self.quitdialog:
                self.quitdialog = QuitSaveDialog(self.mainwin)
            if not self.quitdialog.run(self.config):
                return True
        gtk.main_quit()
        if os.path.exists(self.tmpconfpath):
            os.unlink(self.tmpconfpath)
        # continue to mainwin destroy if called by delete_event
        return False

    # ------- pause callback -----------
    def pause(self, widget):
        if widget.get_active():
            self.hatari.pause()
        else:
            self.hatari.unpause()

    # dialogs
    # ------- reset callback -----------
    def reset(self, widget):
        if not self.resetdialog:
            self.resetdialog = ResetDialog(self.mainwin)
        self.resetdialog.run(self.hatari)

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

    # ------- floppydisk callback -----------
    def floppydisk(self, widget):
        if not self.floppydialog:
            self.floppydialog = FloppyDialog(self.mainwin)
        self.floppydialog.run(self.config)

    # ------- harddisk callback -----------
    def harddisk(self, widget):
        if not self.harddiskdialog:
            self.harddiskdialog = HardDiskDialog(self.mainwin)
        self.harddiskdialog.run(self.config)

    # ------- display callback -----------
    def display(self, widget):
        if not self.displaydialog:
            self.displaydialog = DisplayDialog(self.mainwin)
        self.displaydialog.run(self.config)

    # ------- joystick callback -----------
    def joystick(self, widget):
        if not self.joystickdialog:
            self.joystickdialog = JoystickDialog(self.mainwin)
        self.joystickdialog.run(self.config)

    # ------- machine callback -----------
    def machine(self, widget):
        if not self.machinedialog:
            self.machinedialog = MachineDialog(self.mainwin)
        if self.machinedialog.run(self.config):
            self.hatari.trigger_shortcut("coldreset")

    # ------- peripheral callback -----------
    def peripheral(self, widget):
        if not self.peripheraldialog:
            self.peripheraldialog = PeripheralDialog(self.mainwin)
        self.peripheraldialog.run(self.config)

    # ------- sound callback -----------
    def sound(self, widget):
        if not self.sounddialog:
            self.sounddialog = SoundDialog(self.mainwin)
        self.sounddialog.run(self.config)

    # ------- path callback -----------
    def path(self, widget):
        if not self.pathdialog:
            self.pathdialog = PathDialog(self.mainwin)
        self.pathdialog.run(self.config)

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

    # ------ snapshot load/save callbacks ---------
    def load(self, widget):
        path = os.path.expanduser("~/.hatari/hatari.sav")
        filename = get_open_filename("Select snapshot", self.mainwin, path)
        if filename:
            self.memstate = ["--memstate", filename]
            self.run()
            return True
        return False

    def save(self, widget):
        self.hatari.trigger_shortcut("savemem")

    # ------ config load/save callbacks ---------
    def config_load(self, widget):
        path = self.config.get_path()
        filename = get_open_filename("Select configuration file", self.mainwin, path)
        if filename:
            self.hatari.change_option("--configfile %s" % filename)
            self.config.load(filename)
            self._reset_config_dialogs()
            return True
        return False

    def config_save(self, widget):
        path = self.config.get_path()
        filename = get_save_filename("Save configuration as...", self.mainwin, path)
        if filename:
            self.config.save_as(filename)
            return True
        return False

    # ------- fast forward callback -----------
    def set_fastforward(self, widget):
        self.config.set_fastforward(widget.get_active())

    def get_fastforward(self):
        return self.config.get_fastforward()

    # ------- fullscreen callback -----------
    def set_fullscreen(self, widget):
        # if user can select this, Hatari isn't in fullscreen
        self.hatari.change_option("--fullscreen")

    # ------- screenshot callback -----------
    def screenshot(self, widget):
        self.hatari.trigger_shortcut("screenshot")

    # ------- record callbacks -----------
    def recanim(self, widget):
        self.hatari.trigger_shortcut("recanim")

    def recsound(self, widget):
        self.hatari.trigger_shortcut("recsound")

    # ------- insert key special callback -----------
    def keypress(self, widget, code):
        self.hatari.insert_event("keypress %s" % code)

    def textinsert(self, widget, text):
        HatariTextInsert(self.hatari, text)

    # ------- panel callback -----------
    def panel(self, action, box):
        title = action.get_name()
        if title not in self.panels:
            window = gtk.Window(gtk.WINDOW_TOPLEVEL)
            window.set_transient_for(self.mainwin)
            window.set_icon_from_file(UInfo.icon)
            window.set_title(title)
            window.add(box)
            window.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_DIALOG)
            window.connect("delete_event", window_hide_cb)
            self.panels[title] = window
        else:
            window = self.panels[title]
        window.show_all()
        window.deiconify()


# ---------------------------------------------------------------
# class for creating menus, toolbars and panels
# and managing actions bound to them
class UIActions:
    def __init__(self):
        cb = self.callbacks = UICallbacks()

        self.help = UIHelp()
        
        self.actions = gtk.ActionGroup("All")
        
        # name, icon ID, label, accel, tooltip, callback
        self.actions.add_toggle_actions((
        # TODO: how to know when these are changed from inside Hatari?
        ("recanim", gtk.STOCK_MEDIA_RECORD, "Record animation", "<Ctrl>A", "Record animation", cb.recanim),
        ("recsound", gtk.STOCK_MEDIA_RECORD, "Record sound", "<Ctrl>W", "Record YM/Wav", cb.recsound),
        ("pause", gtk.STOCK_MEDIA_PAUSE, "Pause", "<Ctrl>P", "Pause Hatari to save battery", cb.pause),
        ("forward", gtk.STOCK_MEDIA_FORWARD, "Forward", "<Ctrl>F", "Whether to fast forward Hatari (needs fast machine)", cb.set_fastforward, cb.get_fastforward())
        ))
        
        # name, icon ID, label, accel, tooltip, callback
        self.actions.add_actions((
        ("load", gtk.STOCK_OPEN, "Load snapshot...", "<Ctrl>L", "Load emulation snapshot", cb.load),
        ("save", gtk.STOCK_SAVE, "Save snapshot", "<Ctrl>S", "Save emulation snapshot", cb.save),
        ("shot", gtk.STOCK_MEDIA_RECORD, "Grab screenshot", "<Ctrl>G", "Grab a screenshot", cb.screenshot),
        ("quit", gtk.STOCK_QUIT, "Quit", "<Ctrl>Q", "Quit Hatari UI", cb.quit),
        
        ("run", gtk.STOCK_MEDIA_PLAY, "Run", "<Ctrl>R", "(Re-)run Hatari", cb.run),
        ("full", gtk.STOCK_FULLSCREEN, "Fullscreen", "<Ctrl>U", "Toggle whether Hatari is fullscreen", cb.set_fullscreen),
        ("input", gtk.STOCK_SPELL_CHECK, "Inputs...", "<Ctrl>N", "Simulate text input and mouse clicks", cb.inputs),
        ("reset", gtk.STOCK_REFRESH, "Reset...", "<Ctrl>E", "Warm or cold reset Hatari", cb.reset),
        
        ("display", gtk.STOCK_PREFERENCES, "Display...", "<Ctrl>Y", "Display settings", cb.display),
        ("floppy", gtk.STOCK_FLOPPY, "Floppies...", "<Ctrl>D", "Floppy images", cb.floppydisk),
        ("harddisk", gtk.STOCK_HARDDISK, "Hard disks...", "<Ctrl>H", "Hard disk images and directories", cb.harddisk),
        ("joystick", gtk.STOCK_CONNECT, "Joysticks...", "<Ctrl>J", "Joystick settings", cb.joystick),
        ("machine", gtk.STOCK_HARDDISK, "Machine...", "<Ctrl>M", "Hatari st/e/tt/falcon configuration", cb.machine),
        ("device", gtk.STOCK_PRINT, "Peripherals...", "<Ctrl>V", "Toggle Midi, Printer, RS232 peripherals", cb.peripheral),
        ("sound", gtk.STOCK_PROPERTIES, "Sound...", "<Ctrl>O", "Sound settings", cb.sound),

        ("path", gtk.STOCK_DIRECTORY, "Paths...", None, "Device & save file paths", cb.path),
        ("lconfig", gtk.STOCK_OPEN, "Load config...", "<Ctrl>C", "Load configuration", self.config_load),
        ("sconfig", gtk.STOCK_SAVE_AS, "Save config as...", None, "Save configuration", cb.config_save),
        
        ("debug", gtk.STOCK_FIND, "Debugger...", "<Ctrl>B", "Activate Hatari debugger", cb.debugger),
        ("trace", gtk.STOCK_EXECUTE, "Trace settings...", "<Ctrl>T", "Hatari tracing setup", cb.trace),
        
        ("manual", None, "Hatari manual", None, None, self.help.view_hatari_manual),
        ("compatibility", None, "Hatari compatibility list", None, None, self.help.view_hatari_compatibility),
        ("release", None, "Hatari release notes", None, None, self.help.view_hatari_releasenotes),
        ("todo", None, "Hatari TODO", None, None, self.help.view_hatari_todo),
        ("bugs", None, "Report a bug", None, None, self.help.view_hatari_bugs),
        ("mails", None, "Hatari mailing lists", None, None, self.help.view_hatari_mails),
        ("changes", None, "Latest Hatari changes", None, None, self.help.view_hatari_repository),
        ("authors", None, "Hatari authors", None, None, self.help.view_hatari_authors),
        ("hatari", None, "Hatari home page", None, None, self.help.view_hatari_page),
        ("hatariui", None, "Hatari UI home page", None, None, self.help.view_hatariui_page),
        ("about", gtk.STOCK_INFO, "Hatari UI info", "<Ctrl>I", "Hatari UI information", cb.about)
        ))
        self.action_names = [x.get_name() for x in self.actions.list_actions()]

        # no actions set yet to panels or toolbars
        self.toolbars = {}
        self.panels = []

    def config_load(self, widget):
        # user loads a new configuration?
        if self.callbacks.config_load(widget):
            print("TODO: reset toggle actions")

    # ----- toolbar / panel additions ---------
    def set_actions(self, action_str, place):
        "set_actions(actions,place) -> error string, None if all OK"
        actions = action_str.split(",")
        for action in actions:
            if action in self.action_names:
                # regular action
                continue
            if action in self.panels:
                # user specified panel
                continue
            if action in ("close", ">"):
                if place != "panel":
                    return "'close' and '>' can be only in a panel"
                continue
            if action == "|":
                # divider
                continue
            if action.find("=") >= 0:
                # special keycode/string action
                continue
            return "unrecognized action '%s'" % action

        if place in ("left", "right", "top", "bottom"):
            self.toolbars[place] = actions
            return None
        if place == "panel":
            if len(actions) < 3:
                return "panel has too few items to be useful"
            return None
        return "unknown actions position '%s'" % place

    def add_panel(self, spec):
        "add_panel(panel_specification) -> error string, None if all is OK"
        offset = spec.find(",")
        if offset <= 0:
            return "invalid panel specification '%s'" % spec

        name, panelcontrols = spec[:offset], spec[offset+1:]
        error = self.set_actions(panelcontrols, "panel")
        if error:
            return error

        if ",>," in panelcontrols:
            box = gtk.VBox()
            splitcontrols = panelcontrols.split(",>,")
            for controls in splitcontrols:
                box.add(self._get_container(controls.split(",")))
        else:
            box = self._get_container(panelcontrols.split(","))
            
        self.panels.append(name)
        self.actions.add_actions(
            ((name, gtk.STOCK_ADD, name, None, name, self.callbacks.panel),),
            box
        )
        return None

    def list_actions(self):
        yield ("|", "Separator between controls")
        yield (">", "Next toolbar in panel windows")
        # generate the list from action information
        for act in self.actions.list_actions():
            note = act.get_property("tooltip")
            if not note:
                note = act.get_property("label")
            yield(act.get_name(), note)
        yield ("<panel name>", "Button for the specified panel window")
        yield ("<name>=<string/code>", "Synthetize string or single key <code>")

    # ------- panel special actions -----------
    def _close_cb(self, widget):
        widget.get_toplevel().hide()

    # ------- key special action -----------
    def _create_key_control(self, name, textcode):
        "Simulate Atari key press/release and string inserting"
        if not textcode:
            return None
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
        widget.set_tooltip_text("Insert " + tip)
        return widget

    def _get_container(self, actions, horiz = True):
        "return Gtk container with the specified actions or None for no actions"
        if not actions:
            return None

        #print("ACTIONS:", actions)
        if len(actions) > 1:
            bar = gtk.Toolbar()
            if horiz:
                bar.set_orientation(gtk.ORIENTATION_HORIZONTAL)
            else:
                bar.set_orientation(gtk.ORIENTATION_VERTICAL)
            bar.set_style(gtk.TOOLBAR_BOTH)
            # disable overflow menu to get toolbar sized correctly for panels
            bar.set_show_arrow(False)
        else:
            bar = None
        
        for action in actions:
            #print(action)
            offset = action.find("=")
            if offset >= 0:
                # handle "<name>=<keycode>" action specification
                name = action[:offset]
                text = action[offset+1:]
                widget = self._create_key_control(name, text)
            elif action == "|":
                widget = gtk.SeparatorToolItem()
            elif action == "close":
                if bar:
                    widget = create_toolbutton(gtk.STOCK_CLOSE, self._close_cb)
                else:
                    widget = create_button("Close", self._close_cb)
            else:
                widget = self.actions.get_action(action).create_tool_item()
            if not widget:
                continue
            if bar:
                if action != "|":
                    widget.set_expand(True)
                bar.insert(widget, -1)
        if bar:
            return bar
        return widget

    # ------------- handling menu -------------
    def _add_submenu(self, bar, title, items):
        submenu = gtk.Menu()
        for name in items:
            if name:
                action = self.actions.get_action(name)
                item = action.create_menu_item()
            else:
                item = gtk.SeparatorMenuItem()
            submenu.add(item)
        baritem = gtk.MenuItem(title, False)
        baritem.set_submenu(submenu)
        bar.add(baritem)

    def _get_menu(self):
        allmenus = (
        ("File", ("load", "save", None, "shot", "recanim", "recsound", None, "quit")),
        ("Emulation", ("run", "pause", "forward", None, "full", None, "input", None, "reset")),
        ("Devices", ("display", "floppy", "harddisk", "joystick", "machine", "device", "sound")),
        ("Configuration", ("path", None, "lconfig", "sconfig")),
        ("Debug", ("debug", "trace")),
        ("Help", ("manual", "compatibility", "release", "todo", None, "bugs", "mails", "changes", None, "authors", "hatari", "hatariui", "about",))
        )
        bar = gtk.MenuBar()

        for title, items in allmenus:
            self._add_submenu(bar, title, items)

        if self.panels:
            self._add_submenu(bar, "Panels", self.panels)

        return bar

    # ------------- run the whole UI -------------
    def run(self, floppy, havemenu, fullscreen, embed):
        accelgroup = None
        # create menu?
        if havemenu:
            # this would steal keys from embedded Hatari
            if not embed:
                accelgroup = gtk.AccelGroup()
                for action in self.actions.list_actions():
                    action.set_accel_group(accelgroup)
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

        self.callbacks.create_ui(accelgroup, menu, toolbars, fullscreen, embed)
        self.help.set_mainwin(self.callbacks.mainwin)
        self.callbacks.set_floppy(floppy)

        # ugly, Hatari socket window ID can be gotten only
        # after Socket window is realized by gtk_main()
        gobject.idle_add(self.callbacks.run)
        gtk.main()


# ------------- usage / argument handling --------------
def usage(actions, msg=None):
    name = os.path.basename(sys.argv[0])
    uiname = "%s %s" % (UInfo.name, UInfo.version)
    print("\n%s" % uiname)
    print("=" * len(uiname))
    print("\nUsage: %s [options] [directory|disk image|Atari program]" % name)
    print("\nOptions:")
    print("\t-h, --help\t\tthis help")
    print("\t-n, --nomenu\t\tomit menus")
    print("\t-e, --embed\t\tembed Hatari window in middle of controls")
    print("\t-f, --fullscreen\tstart in fullscreen")
    print("\t-l, --left <controls>\ttoolbar at left")
    print("\t-r, --right <controls>\ttoolbar at right")
    print("\t-t, --top <controls>\ttoolbar at top")
    print("\t-b, --bottom <controls>\ttoolbar at bottom")
    print("\t-p, --panel <name>,<controls>")
    print("\t\t\t\tseparate window with given name and controls")
    print("\nAvailable (panel/toolbar) controls:")
    for action, description in actions.list_actions():
        size = len(action)
        if size < 8:
            tabs = "\t\t"
        elif size < 16:
            tabs = "\t"
        else:
            tabs = "\n\t\t\t"
        print("\t%s%s%s" % (action, tabs, description))
    print("""
You can have as many panels as you wish.  For each panel you need to add
a control with the name of the panel (see "MyPanel" below).

For example:
\t%s --embed \\
\t-t "about,run,pause,quit" \\
\t-p "MyPanel,Macro=Test,Undo=97,Help=98,>,F1=59,F2=60,F3=61,F4=62,>,close" \\
\t-r "paste,debug,trace,machine,MyPanel" \\
\t-b "sound,|,fastforward,|,fullscreen"

if no options are given, the UI uses basic controls.
""" % name)
    if msg:
        print("ERROR: %s\n" % msg)
    sys.exit(1)


def main():
    info = UInfo()
    actions = UIActions()
    try:
        longopts = ["embed", "fullscreen", "nomenu", "help",
            "left=", "right=", "top=", "bottom=", "panel="]
        opts, floppies = getopt.getopt(sys.argv[1:], "efnhl:r:t:b:p:", longopts)
        del longopts
    except getopt.GetoptError as err:
        usage(actions, err)

    menu = True
    embed = False
    fullscreen = False

    error = None
    for opt, arg in opts:
        print(opt, arg)
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

    if len(floppies) > 1:
        usage(actions, "multiple floppy images given: %s" % str(floppies))
    if floppies:
        if not os.path.exists(floppies[0]):
            usage(actions, "floppy image '%s' doesn't exist" % floppies[0])

    actions.run(floppies, menu, fullscreen, embed)


if __name__ == "__main__":
    main()
