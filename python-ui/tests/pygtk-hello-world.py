#!/usr/bin/env python

from gi.repository import Gtk

class AppUI():
    def __init__(self):
        self.window = Gtk.Window(Gtk.WindowType.TOPLEVEL)
        self.window.connect("destroy", Gtk.main_quit)

        label = Gtk.Label(label="Hello World!")
        self.window.add(label)

    def run(self):
        self.window.show_all()
        Gtk.main()

app = AppUI()
app.run()
