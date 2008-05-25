#!/usr/bin/env python
#
# Classes for Hatari emulator instance and mapping its congfiguration
# variables with its command line option.
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
import time
import signal
import socket
import select
from config import ConfigStore


# Running Hatari instance
class Hatari():
    basepath = "/tmp/hatari-ui-" + os.getenv("USER")
    logpath = basepath + ".log"
    tracepath = basepath + ".trace"
    debugpath = basepath + ".debug"
    controlpath = basepath + ".socket"
    server = None # singleton due to path being currently one per user

    def __init__(self, hataribin = None):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        if hataribin:
            self.hataribin = hataribin
        else:
            self.hataribin = "hatari"
        self.create_server()
        self.control = None
        self.paused = False
        self.pid = 0

    def create_server(self):
        if self.server:
            return
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        if os.path.exists(self.controlpath):
            os.unlink(self.controlpath)
        self.server.bind(self.controlpath)
        self.server.listen(1)

    def send_message(self, msg):
        if self.control:
            self.control.send(msg)
            return True
        else:
            print "ERROR: no Hatari (control socket)"
            return False
        
    def change_option(self, option):
        return self.send_message("hatari-option %s\n" % option)

    def trigger_shortcut(self, shortcut):
        return self.send_message("hatari-shortcut %s\n" % shortcut)

    def insert_event(self, event):
        return self.send_message("hatari-event %s\n" % event)

    def debug_command(self, cmd):
        return self.send_message("hatari-debug %s\n" % cmd)

    def pause(self):
        return self.send_message("hatari-stop\n")

    def unpause(self):
        return self.send_message("hatari-cont\n")
    
    def open_output_file(self, hataricommand, option, path):
        if os.path.exists(path):
            os.unlink(path)
        # TODO: why fifo doesn't work properly (blocks forever on read or
        #       reads only byte at the time and stops after first newline)?
        #os.mkfifo(path)
        #raw_input("attach strace now, then press Enter\n")
        
        # ask Hatari to open/create the requested output file...
        hataricommand("%s %s" % (option, path))
        wait = 0.025
        # ...and wait for it to appear before returning it
        for i in range(0, 8):
            time.sleep(wait)
            if os.path.exists(path):
                return open(path, "r")
            wait += wait
        return None

    def open_debug_output(self):
        return self.open_output_file(self.debug_command, "f", self.debugpath)

    def open_trace_output(self):
        return self.open_output_file(self.change_option, "--trace-file", self.tracepath)

    def open_log_output(self):
        return self.open_output_file(self.change_option, "--log-file", self.logpath)
    
    def get_lines(self, fileobj):
        # wait until data is available, then wait for some more
        # and only then the data can be read, otherwise its old
        print "Request&wait data from Hatari..."
        select.select([fileobj], [], [])
        time.sleep(0.1)
        print "...read the data lines"
        lines = fileobj.readlines()
        print "".join(lines)
        return lines

    def is_running(self):
        if not self.pid:
            return False
        try:
            os.waitpid(self.pid, os.WNOHANG)
        except OSError, value:
            print "Hatari PID %d had exited in the meanwhile:\n\t%s" % (self.pid, value)
            self.pid = 0
            return False
        return True
    
    def run(self, parent_win = None, embed_args = None):
        # if parent_win given, embed Hatari to it
        pid = os.fork()
        if pid < 0:
            print "ERROR: fork()ing Hatari failed!"
            return
        if pid:
            # in parent
            self.pid = pid
            if self.server:
                print "WAIT hatari to connect to control socket...",
                (self.control, addr) = self.server.accept()
                print "connected!"
        else:
            # child runs Hatari
            env = os.environ
            args = (self.hataribin, )
            if parent_win:
                self.set_embed_env(env, parent_win)
                args += embed_args
            if self.server:
                args += ("--control-socket", self.controlpath)
            print "RUN:", args
            os.execvpe(self.hataribin, args, env)

    def set_embed_env(self, env, parent_win):
        if sys.platform == 'win32':
            win_id = parent_win.handle
        else:
            win_id = parent_win.xid
        # tell SDL to use given widget's window
        #env["SDL_WINDOWID"] = str(win_id)

        # above is broken: when SDL uses a window it hasn't created itself,
        # it for some reason doesn't listen to any events delivered to that
        # window nor implements XEMBED protocol to get them in a way most
        # friendly to embedder:
        #   http://standards.freedesktop.org/xembed-spec/latest/
        #
        # Instead we tell hatari to reparent itself after creating
        # its own window into this program widget window
        env["PARENT_WIN_ID"] = str(win_id)

    def kill(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.pid
            self.pid = 0


# Mapping of requested values both to Hatari configuration
# and command line options.
#
# By default this doesn't allow setting any other configuration
# variables than the ones that were read from the configuration
# file i.e. you get an exception if configuration variables
# don't match to current Hatari.  So before using this you should
# have saved Hatari configuration at least once.
#
# Because of some inconsistencies in the values (see e.g. sound),
# this cannot just do these according to some mapping table, but
# it needs actual method for (each) setting.
class HatariConfigMapping(ConfigStore):
    def __init__(self, hatari):
        defaults = ({}, {})
        ConfigStore.__init__(self, defaults, "hatari.cfg")
        self.hatari = hatari

    # ------------ fastforward ---------------
    def get_fastforward(self):
        return self.variables.bFastForward

    def set_fastforward(self, value):
        self.variables.bFastForward = value
        self.hatari.change_option("--fast-forward %s" % str(value))
        
    # ------------ spec512 ---------------
    def get_spec512threshold(self):
        return self.variables.nSpec512Threshold

    def set_spec512threshold(self, value):
        value = int(value) # guarantee correct type
        self.variables.nSpec512Threshold = value
        self.hatari.change_option("--spec512 %d" % value)
        
    # ------------ sound ---------------
    def get_sound_values(self):
        return ("Off", "11kHz", "22kHz", "44kHz")
    
    def get_sound(self):
        # return index to get_sound_values() array
        if self.variables.bEnableSound:
            return self.variables.nPlaybackQuality + 1
        return 0

    def set_sound(self, value):
        # map get_sound_values() index to Hatari config
        if value:
            self.variables.nPlaybackQuality = value - 1
            self.variables.bEnableSound = True
        else:
            self.variables.bEnableSound = False
        # and to cli option
        quality = { 0: "off", 1: "low", 2: "med", 3: "hi" }[value]
        self.hatari.change_option("--sound %s" % quality)

    # ------------ frameskips ---------------
    def get_frameskips(self):
        return self.variables.nFrameSkips
    
    def set_frameskips(self, value):
        value = int(value) # guarantee correct type
        self.variables.nFrameSkips = value
        self.hatari.change_option("--frameskips %d" % value)

    # ------------ options to embed to requested size ---------------
    def get_embed_args(self, size):
        print "TODO: save and use temporary Hatari-UI hatari settings?"
        # need to modify Hatari settings to match the given window size
        (width, height) = size
        if width < 320 or height < 200:
            print "ERROR: Hatari needs larger than %dx%d window" % (width, height)
            sys.exit(1)
        
        # for VDI we can use (fairly) exact size + ignore other screen options
        if self.variables.bUseExtVdiResolutions:
            return ("--vdi-width", str(width), "--vdi-height", str(height))
        
        args = ()
        # max size with overscan borders
        border_minw = self.variables.nWindowBorderPixelsLeft + 320 + self.variables.nWindowBorderPixelsRight
        border_minh = 29 + 200 + self.variables.nWindowBorderPixelsBottom
        if width < 640 or height < 400:
            # only non-zoomed color mode fits to window
            args = ("--zoom", "1", "--monitor", "vga")
            if width < border_minw and height < border_minh:
                # without borders
                args += ("--borders", "off")
        else:
            borders = self.variables.bAllowOverscan
            mono = (self.variables.nMonitorType == 0)
            zoom = self.variables.bZoomLowRes
            # can we have overscan borders with color zooming or mono?
            if borders and (width < 2*border_minw or height < 2*border_minh):
                if mono:
                    # mono -> no border
                    args = ("--borders", "off")
                elif zoom:
                    # color -> no zoom, just border
                    args = ("--zoom", "1")
            elif not (mono or zoom):
                # no mono nor zoom -> zoom
                args = ("--zoom", "2")
        if self.variables.bFullScreen:
            # fullscreen Hatari doesn't make sense with Hatari UI
            args += ("--window", )

        # window size for other than ST & STE can differ
        if self.variables.nMachineType not in (0, 1):
            print "WARNING: neither ST nor STE, forcing machine to ST"
            args += ("--machine", "st")

        return args
