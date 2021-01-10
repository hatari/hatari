#!/usr/bin/env python3
#
# To test Git version of Hatari, use something like:
#   PATH=../../build/src:$PATH ./gtk-hatari-embed-test.py ...
#
# Tests embedding hatari with three different methods:
# "hatari": ask Hatari to reparent to given window
# "reparent": Find Hatari window and reparent it into pygtk widget in python
#   - Needs "xwininfo" and "awk" i.e. not real alternative
#
# Using three alternative widgets:
#   "drawingarea"
#   "eventbox"
#   "socket"
#
# Results:
#   "drawingarea" & "evenbox" with "hatari":
#     -> XCB fails unknown seq num when importing Hatari window
#   "drawingarea" & "evenbox" with "reparent":
#     -> Hatari window opens outside of test app before reparented
#     -> keyboard input doesn't work
#   "socket" with "reparent":
#     -> Hatari window opens outside of test app before reparented
#   "socket" with "hatari":
#     -> only method working flawlessly
import os
import sys
import time

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GdkX11
from gi.repository import GLib

def usage(error):
    print("\nusage: %s <widget> <embed method>\n" % sys.argv[0].split(os.path.sep)[-1])
    print("Opens window with given <widget>, runs Hatari and tries to embed it")
    print("with given <method>\n")
    print("<widget> can be <drawingarea|eventbox|socket>")
    print("<method> can be <hatari|reparent>\n")
    print("ERROR: %s\n" % error)
    sys.exit(1)


class AppUI():
    hatari_wd = 640
    hatari_ht = 436 # Hatari window enables statusbar by default

    def __init__(self, widget, method):
        if method in ("hatari", "reparent"):
            self.method = method
        else:
            usage("unknown <method> '%s'" % method)
        if widget == "drawingarea":
            widgettype = Gtk.DrawingArea
        elif widget == "eventbox":
            widgettype = Gtk.EventBox
        elif widget == "socket":
            # XEMBED socket for Hatari/SDL
            widgettype = Gtk.Socket
        else:
            usage("unknown <widget> '%s'" % widget)
        self.window = self.create_window()
        self.add_hatari_parent(self.window, widgettype)
        self.window.show_all()
        # wait a while before starting Hatari to make
        # sure parent window has been realized
        GLib.timeout_add(500, self.timeout_cb)

    def create_window(self):
        window = Gtk.Window(type=Gtk.WindowType.TOPLEVEL)
        window.connect("destroy", self.do_quit)
        return window

    def do_quit(self, widget):
        if self.hatari_pid:
            os.kill(self.hatari_pid, 9)
            print("killed Hatari PID %d" % self.hatari_pid)
            self.hatari_pid = 0
        Gtk.main_quit()

    def add_hatari_parent(self, parent, widgettype):
        # Note: CAN_FOCUS has to be set for the widget embedding Hatari
        # and *unset* for everything else, otherwise Hatari doesn't
        # receive *any* keyevents.
        self.hatari_pid = 0
        vbox = Gtk.VBox()
        button = Gtk.Button(label="Test Button", can_focus=False)
        vbox.add(button)
        widget = widgettype(can_focus=True)
        widget.set_size_request(self.hatari_wd, self.hatari_ht)
        widget.set_events(Gdk.EventMask.ALL_EVENTS_MASK)
        self.hatariparent = widget
        # TODO: when running 320x200, parent could be centered to here
        vbox.add(widget)
        # test focus
        label = Gtk.Label(label="Test SpinButton:")
        vbox.add(label)
        # disable focus, otherwise Hatari doesn't receive keys!!!
        spin = Gtk.SpinButton(can_focus=False)
        spin.set_range(0, 10)
        spin.set_digits(0)
        spin.set_numeric(True)
        spin.set_increments(1, 2)
        vbox.add(spin)
        parent.add(vbox)

    def timeout_cb(self):
        self.do_hatari_method()
        return False # only once

    def do_hatari_method(self):
        pid = os.fork()
        if pid < 0:
            print("ERROR: fork()ing Hatari failed!")
            return
        if pid:
            # in parent
            if self.method == "reparent":
                hatari_win = self.find_hatari_window()
                if hatari_win:
                    self.reparent_hatari_window(hatari_win)
                    self.hatari_pid = pid
                else:
                    os.kill(pid, signal.SIGKILL)
                    print("killed process with PID %d" % pid)
                    self.hatari_pid = 0
            else:
                print("Waiting Hatari process to embed itself...")
                self.hatari_pid = pid
        else:
            # child runs Hatari
            args = ("hatari", "-m")
            os.execvpe("hatari", args, self.get_hatari_env())

    def get_hatari_env(self):
        if self.method == "reparent":
            return os.environ
        # tell Hatari to embed itself inside given widget's window
        win_id = self.hatariparent.get_window().get_xid()
        env = os.environ
        env["PARENT_WIN_ID"] = str(win_id)
        return env

    def find_hatari_window(self):
        # find hatari window by its WM class string and reparent it
        # wait 1s to make sure Hatari child gets its window up
        cmd = """sleep 1; xwininfo -root -tree|awk '/"hatari" "hatari"/{print $1}'"""
        counter = 0
        while counter < 8:
            pipe = os.popen(cmd)
            windows = []
            for line in pipe.readlines():
                windows.append(int(line, 16))
            try:
                pipe.close()
            except IOError:
                # handle child process exiting silently
                pass
            if not windows:
                counter += 1
                print("WARNING: no Hatari window found yet, retrying...")
                time.sleep(1)
                continue
            if len(windows) > 1:
                print("WARNING: multiple Hatari windows, picking first one...")
            return windows[0]
        print("ERROR: no windows with the 'hatari' WM class found")
        return None

    def reparent_hatari_window(self, hatari_win):
        print("Importing foreign (Hatari) window 0x%x" % hatari_win)
        display = GdkX11.X11Display.get_default()
        window = GdkX11.X11Window.foreign_new_for_display(display, hatari_win)
        if not window:
            print("ERROR: X window importing failed!")
            return False
        parent = self.hatariparent.get_window()
        if not window:
            print("ERROR: where hatariparent window disappeared?")
            return False
        print("Found Hatari window ID: 0x%x, reparenting..." % hatari_win)
        print("...to container window ID: 0x%x" % parent.get_xid())
        window.reparent(parent, 0, 0)
        #window.reparent(self.hatariparent.get_toplevel().window, 0, 0)
        #window.reparent(self.hatariparent.get_root_window(), 0, 0)
        #window.show()
        #window.raise_()
        # If python would destroy the Gtk widget when it goes out of scope,
        # the foreign window widget destructor would delete Hatari window.
        # So, keep a reference
        #self.hatariwindow = window
        return True

    def run(self):
        self.window.show_all()
        Gtk.main()


if len(sys.argv) != 3:
    usage("wrong number of arguments")
app = AppUI(sys.argv[1], sys.argv[2])
app.run()
