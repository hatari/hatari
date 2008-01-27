#!/usr/bin/env python

import gtk

class AppUI():
    def __init__(self):
        self.window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        self.window.connect("destroy", gtk.main_quit)
        
        label = gtk.Label("Hello World!")
        self.window.add(label)
        
    def run(self):
        self.window.show_all()
        gtk.main()
        
app = AppUI()
app.run()
