#!/usr/bin/python
#
# Tests embedding hatari with three different methods:
# "hatari": ask Hatari to reparent to given window
# "sdl": Give SDL window into which it should reparent
#   -> SDL doesn't handle (mouse, key, expose) events
#      although according to "xev" it's window receives them!
#      Bug in SDL (not one of the originally needed features?)?
# "reparent": Find Hatari window and reparent it into pygtk widget in python
#   - Needs "xwininfo" and "awk"
#
# Using three alternative widgets:
#   "drawingarea"
#   "eventbox"
#   "socket"
#
# Results:
#   reparent+eventbox
#     -> PyGtk reparents Hatari under something on rootwindow instead
#        (reparening eventbox under Hatari window works fine though...)
#   reparent+socket
#     -> Hatari seems to be reparented back to where it was
#   sdl+anything
#     -> all events are lost
#   hatari+socket
#     -> seems to work fine
import os
import sys
import gtk
import time
import gobject

def usage(error):
    print "\nusage: %s <widget> <embed method>\n" % sys.argv[0].split(os.path.sep)[-1]
    print "Opens window with given <widget>, runs Hatari and tries to embed it"
    print "with given <method>\n"
    print "<widget> can be <drawingarea|eventbox|socket>"
    print "<method> can be <sdl|hatari|reparent>\n"
    print "ERROR: %s\n" % error
    sys.exit(1)


class AppUI():
    hatari_wd = 640
    hatari_ht = 400
    
    def __init__(self, widget, method):
        if method in ("hatari", "reparent", "sdl"):
            self.method = method
        else:
            usage("unknown <method> '%s'" % method)
        if widget == "drawingarea":
            widgettype = gtk.DrawingArea
        elif widget == "eventbox":
            widgettype = gtk.EventBox
        elif widget == "socket":
            # XEMBED socket for Hatari/SDL
            widgettype = gtk.Socket
        else:
            usage("unknown <widget> '%s'" % widget)
        self.window = self.create_window()
        self.add_hatari_parent(self.window, widgettype)
        gobject.timeout_add(1*1000, self.timeout_cb)
        
    def create_window(self):
        window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        window.connect("destroy", self.do_quit)
        return window
    
    def do_quit(self, widget):
        if self.hatari_pid:
            os.kill(self.hatari_pid, 9)
            print "killed Hatari PID %d" % self.hatari_pid
            self.hatari_pid = 0
        gtk.main_quit()
    
    def add_hatari_parent(self, parent, widgettype):
        # Note: CAN_FOCUS has to be set for the widget embedding Hatari
        # and *unset* for everything else, otherwise Hatari doesn't
        # receive *any* keyevents.
        self.hatari_pid = 0
        vbox = gtk.VBox()
        button = gtk.Button("Test Button")
        button.unset_flags(gtk.CAN_FOCUS)
        vbox.add(button)
        widget = widgettype()
        widget.set_size_request(self.hatari_wd, self.hatari_ht)
        widget.set_events(gtk.gdk.ALL_EVENTS_MASK)
        widget.set_flags(gtk.CAN_FOCUS)
        self.hatariparent = widget
        # TODO: when running 320x200, parent could be centered to here
        vbox.add(widget)
        # test focus
        label = gtk.Label("Test SpinButton:")
        vbox.add(label)
        spin = gtk.SpinButton()
        spin.set_range(0, 10)
        spin.set_digits(0)
        spin.set_numeric(True)
        spin.set_increments(1, 2)
        # otherwise Hatari doesn't receive keys!!!
        spin.unset_flags(gtk.CAN_FOCUS)
        vbox.add(spin)
        parent.add(vbox)
    
    def timeout_cb(self):
        self.do_hatari_method()
        return False # only once
    
    def do_hatari_method(self):
        pid = os.fork()
        if pid < 0:
            print "ERROR: fork()ing Hatari failed!"
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
                    print "killed process with PID %d" % pid
                    self.hatari_pid = 0
            else:
                # method == "sdl" or "hatari"
                self.hatari_pid = pid
        else:
            # child runs Hatari
            args = ("hatari", "-m", "-z", "2")
            os.execvpe("hatari", args, self.get_hatari_env())

    def get_hatari_env(self):
        if self.method == "reparent":
            return os.environ
        # tell SDL to use (embed itself inside) given widget's window
        win_id = self.hatariparent.window.xid
        env = os.environ
        if self.method == "sdl":
            env["SDL_WINDOWID"] = str(win_id)
        elif self.method == "hatari":
            env["HATARI_PARENT_WIN"] = str(win_id)
        return env
    
    def find_hatari_window(self):
        # find hatari window by its WM class string and reparent it
        cmd = """xwininfo -root -tree|awk '/"hatari" "hatari"/{print $1}'"""
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
                print "WARNING: no Hatari window found yet, retrying..."
                time.sleep(1)
                continue
            if len(windows) > 1:
                print "WARNING: multiple Hatari windows, picking first one..."
            return windows[0]
        print "ERROR: no windows with the 'hatari' WM class found"
        return None
    
    def reparent_hatari_window(self, hatari_win):
        window = gtk.gdk.window_foreign_new(hatari_win)
        if not window:
            print "ERROR: Hatari window (ID: 0x%x) reparenting failed!" % hatari_win
            return False
        if not self.hatariparent.window:
            print "ERROR: where hatariparent disappeared?"
            return False
        print "Found Hatari window ID: 0x%x, reparenting..." % hatari_win
        print "...to container window ID: 0x%x" % self.hatariparent.window.xid
        window.reparent(self.hatariparent.window, 0, 0)
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
        gtk.main()


if len(sys.argv) != 3:
    usage("wrong number of arguments")
app = AppUI(sys.argv[1], sys.argv[2])
app.run()
