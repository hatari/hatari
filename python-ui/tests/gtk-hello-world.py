#!/usr/bin/env python3

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk

class AppUI():
    def __init__(self):
        self.window = Gtk.Window(type=Gtk.WindowType.TOPLEVEL)
        self.window.connect("destroy", Gtk.main_quit)

        label = Gtk.Label(label="Hello World!")
        self.window.add(label)

    def run(self):
        self.window.show_all()
        Gtk.main()

app = AppUI()
app.run()
