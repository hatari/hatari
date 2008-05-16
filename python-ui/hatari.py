#!/usr/bin/env python
#
# Classes for Hatari configuration and emulator instance
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
    
    def get_lines(self, file):
        # wait until data is available, then wait for some more
        # and only then the data can be read, otherwise its old
        print "Request&wait data from Hatari..."
        select.select([file], [], [])
        time.sleep(0.1)
        print "...read the data lines"
        lines = file.readlines()
        print "".join(lines)
        return lines

    def is_running(self):
        if not self.pid:
            return False
        try:
            pid,status = os.waitpid(self.pid, os.WNOHANG)
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


# Current Hatari configuration.
#
# The configuration variable names are unique,
# so internally this doesn't need to use sections,
# only when loading/saving.
class Config():
    def __init__(self, path = None):
        self.changed = False
        if not path:
            path = self.get_path()
        if path:
            self.keys, self.sections = self.load(path)
            if self.keys:
                print "Loaded Hatari configuration file:", path
                #self.write(sys.stdout)
            else:
                print "WARNING: Hatari configuration file '%' loading failed" % path
                path = None
        else:
            print "Hatari configuration file missing"
            self.sections = {}
            self.keys = {}
        self.path = path
        # take copy of the original settings so that we know what changed
        self.original = self.keys.copy()

    def get_path(self):
        # hatari.cfg can be in home or current work dir
        for path in (os.getenv("HOME"), os.getcwd()):
            if path:
                path = self.check_path(path)
                if path:
                    return path
        return None

    def check_path(self, path):
        # check path/.hatari/hatari.cfg, path/hatari.cfg
        path += os.path.sep
        testpath = path + ".hatari" + os.path.sep + "hatari.cfg"
        if os.path.exists(testpath):
            return testpath
        testpath = path + "hatari.cfg"
        if os.path.exists(testpath):
            return testpath
        return None
    
    def load(self, path):
        config = open(path, "r")
        if not config:
            return ({}, {})
        name = "[_orphans_]"
        sections = {}
        allkeys = {}
        keys = {}
        for line in config.readlines():
            line = line.strip()
            if not line or line[0] == '#':
                continue
            if line[0] == '[':
                if line in sections:
                    print "WARNING: section '%s' twice in configuration" % line
                if keys:
                    sections[name] = keys.keys()
                    keys = {}
                name = line
                continue
            if line.find('=') < 0:
                print "WARNING: line without key=value pair:\n%s" % line
                continue
            key, value = [string.strip() for string in line.split('=')]
            allkeys[key] = value
            keys[key] = value
        if keys:
            sections[name] = keys.keys()
        return allkeys, sections
    
    def get(self, key):
        if key not in self.keys:
            print "WARNING: unknown key '%s'" % key
            return None
        return self.keys[key]
        
    def set(self, key, value):
        oldvalue = self.get(key)
        if not oldvalue:
            # can only set values which have been loaded
            return False
        value = str(value)
        if value != oldvalue:
            self.keys[key] = value
            self.changed = True
        return True

    def is_changed(self):
        return self.changed

    def list_changes(self):
        "return (key, value) tuple for each change config option"
        changed = []
        if self.changed:
            for key,value in self.keys.items():
                if value != self.original[key]:
                    changed.append((key, value))
        return changed

    def revert(self, key):
        self.keys[key] = self.original[key]
    
    def write(self, file):
        sections = self.sections.keys()
        sections.sort()
        for section in sections:
            file.write("%s\n" % section)
            keys = self.sections[section]
            keys.sort()
            for key in keys:
                file.write("%s = %s\n" % (key, self.keys[key]))
            file.write("\n")
            
    def save(self):
        if not self.path:
            print "WARNING: no existing Hatari configuration to modify, saving canceled"
            return
        if not self.changed:
            print "No configuration changes to save, skipping"
            return            
        #file = open(self.path, "w")
        print "TODO: for now writing config to stdout"
        file = sys.stdout
        if file:
            self.write(file)
            print "Saved Hatari configuration file:", self.path
        else:
            print "ERROR: opening '%s' for saving failed" % self.path


# Mapping of requested values both to Hatari configuration
# and command line options.
#
# Because of some inconsistensies in the values (see e.g. sound),
# this cannot just do these according to some mapping, but it
# needs actual method for (each) setting.
class ConfigMapping(Config):
    def __init__(self, hatari):
        Config.__init__(self)
        self.hatari = hatari

    # ------------ fastforward ---------------
    def get_fastforward(self):
        if self.get("bFastForward") != "0":
            return True
        return False

    def set_fastforward(self, value):
        if value:
            print "Entering hyper speed!"
            value = self.set("bFastForward", 1)
            self.hatari.change_option("--fast-forward on")
        else:
            print "Returning to normal speed"
            value = self.set("bFastForward", 0)
            self.hatari.change_option("--fast-forward off")
        
    # ------------ spec512 ---------------
    def get_spec512threshold(self):
        value = self.get("nSpec512Threshold")
        if value:
            return int(value)
        return 0

    def set_spec512threshold(self, value):
        print "Spec512 support:", value
        self.set("nSpec512Threshold", value)
        self.hatari.change_option("--spec512 %d" % value)
        
    # ------------ sound ---------------
    def get_sound_values(self):
        return ("Off", "11kHz", "22kHz", "44kHz")
    
    def get_sound(self):
        # return index to get_sound_values() array
        if self.get("bEnableSound").upper() == "TRUE":
            return int(self.get("nPlaybackQuality")) + 1
        return 0

    def set_sound(self, value):
        # map get_sound_values() index to Hatari config
        if value:
            enabled = "True"
        else:
            enabled = "False"
        self.set("bEnableSound", enabled)
        self.set("nPlaybackQuality", value - 1)
        # and to cli option
        levels = { 0: "off", 1: "low", 2: "med", 3: "hi" }
        quality = levels[value]
        print "Sound (quality):", quality
        self.hatari.change_option("--sound %s" % quality)

    # ------------ frameskips ---------------
    def get_frameskips(self):
        value = self.get("nFrameSkips")
        if value:
            return int(value)
        return 0
    
    def set_frameskips(self, value):
        value = int(value)
        print "Frameskip value:", value
        self.set("nFrameSkips", value)
        self.hatari.change_option("--frameskips %d" % value)

    # ------------ options to embed to requested size ---------------
    def get_embed_args(self, size):
        print "TODO: save and use temporary Hatari-UI hatari settings?"
        # need to modify Hatari settings to match the given window size
        (wd, ht) = size
        if wd < 320 or ht < 200:
            print "ERROR: Hatari needs larger than %dx%d window" % (wd, ht)
            os.exit(1)
        
        # for VDI we can use (fairly) exact size + ignore other screen options
        usevdi = self.get("bUseExtVdiResolutions")
        if usevdi and usevdi.upper() == "TRUE":
            return ("--vdi-width", str(wd), "--vdi-height", str(ht))
        
        print "TODO: get actual Hatari window border size(s) from the configuration file"
         # max border size
        border = 48
        args = ()
        if wd < 640 or ht < 400:
            # only non-zoomed color mode fits to window
            args = ("--zoom", "1", "--monitor", "vga")
            if wd < 320+border*2 and ht < 200+border*2:
                # without borders
                args += ("--borders", "off")
        else:
            zoom = self.get("bZoomLowRes")
            monitor = self.get("nMonitorType")
            useborder = self.get("bAllowOverscan")
            # can we have borders with color zooming or mono?
            if ((wd < 2*(320+border*2) or ht < 2*(200+border*2)) and
                (useborder and useborder.upper() == "TRUE")):
                    if monitor and monitor == "0":
                        # mono -> no border
                        args = ("--borders", "off")
                    elif zoom and zoom.upper() == "TRUE":
                        # color -> no zoom, just border
                        args = ("--zoom", "1")
            elif ((monitor and monitor != "0") or
                  (zoom and zoom.upper() != "TRUE")):
                # no mono nor zoom -> zoom
                args = ("--zoom", "2")

        # window size for other than ST & STE can differ
        machine = self.get("nMachineType")
        if machine != '0' and machine != '1':
            print "WARNING: neither ST nor STE, forcing machine to ST"
            args += ("--machine", "st")
                    
        return args
