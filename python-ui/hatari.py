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
import signal

# running Hatari instance
class Hatari():
    def __init__(self, hataribin = None):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        if hataribin:
            self.hataribin = hataribin
        else:
            self.hataribin = "hatari"
        self.paused = False
        self.pid = 0

    def is_running(self):
        if not self.pid:
            return False
        try:
            pid,status = os.waitpid(self.pid, os.WNOHANG)
        except OSError, value:
            print "Hatari had exited in the meanwhile:\n\t", value
            self.pid = 0
            return False
        return True
    
    def run(self, config, parent_win = None):
        # if parent_win given, embed Hatari to it
        pid = os.fork()
        if pid < 0:
            print "ERROR: fork()ing Hatari failed!"
            return
        if pid:
            # in parent
            self.pid = pid
        else:
            # child runs Hatari
            env = self.get_env(parent_win)
            args = (self.hataribin, ) + self.get_extra_args(config, parent_win)
            print "RUN:", args
            os.execvpe(self.hataribin, args, env)

    def get_env(self, parent_win):
        env = os.environ
        if not parent_win:
            return env
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
        return env

    def get_extra_args(self, config, parent_win):
        print "TODO: save and use temporary Hatari-UI hatari settings?"
        if not parent_win:
            return ()
        # need to modify Hatari settings to match parent window
        #
        # TODO: should check for sizes with borders and make
        # sure "--machine" type is OK (TT and Falcon can have
        # larger screens)
        size = parent_win.get_size()
        if size == (320, 200):
            args = ("--borders", "off", "--zoom", "1", "--monitor", "vga")
        elif size == (640, 400):
            args = ("--borders", "off", "--zoom", "2")
        else:
            print "ERROR: unknown Hatari parent window size", size
            sys.exit(1)
        if config:
            machine = config.get("[System]", "nMachineType")
            # window size for other than ST & STE can differ
            if machine != '0' and machine != '1':
                print "WARNING: neither ST nor STE, forcing machine to ST"
                args += ("--machine", "st")
        return args

    def pause(self):
        if self.pid and not self.paused:
            os.kill(self.pid, signal.SIGSTOP)
            print "paused hatari with PID %d" % self.pid
            self.paused = True
            return True
        return False
    
    def unpause(self):
        if self.pid and self.paused:
            os.kill(self.pid, signal.SIGCONT)
            print "continued hatari with PID %d" % self.pid
            self.paused = False
        
    def stop(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.pid
            self.pid = 0


# current Hatari configuration
class Config():
    def __init__(self, path = None):
        self.changed = False
        if not path:
            path = self.get_path()
        if path:
            self.sections = self.load(path)
            if self.sections:
                print "Loaded Hatari configuration file:", path
                #self.write(sys.stdout)
            else:
                print "WARNING: Hatari configuration file '%' loading failed" % path
                path = None
        else:
            print "Hatari configuration file missing"
            self.sections = {}
        self.path = path

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
        sections = {}
        config = open(path, "r")
        if not config:
            return sections
        name = "[_orphans_]"
        keys = {}
        for line in config.readlines():
            line = line.strip()
            if not line or line[0] == '#':
                continue
            if line[0] == '[':
                if line in sections:
                    print "WARNING: section '%s' twice in configuration" % line
                if keys:
                    sections[name] = keys
                    keys = {}
                name = line
                continue
            if line.find('=') < 0:
                print "WARNING: line without key=value pair:\n%s" % line
                continue
            key, value = line.split('=')
            keys[key.strip()] = value.strip()
        if keys:
            sections[name] = keys
        return sections

    def get(self, section, key):
        if section not in self.sections:
            print "WARNING: unknown section '%s'" % section
            return None
        if key not in self.sections[section]:
            print "WARNING: unknown key '%s[%s]'" % (section, key)
            return None
        return self.sections[section][key]
        
    def set(self, section, key, value):
        oldvalue = self.get(section, key)
        if not oldvalue:
            # can only set values which have been loaded
            return False
        if value != oldvalue:
            self.sections[section][key] = value
            self.changed = True
        return True

    def write(self, file):
        sections = self.sections.keys()
        sections.sort()
        for section in sections:
            file.write("%s\n" % section)
            items = self.sections[section]
            keys = items.keys()
            keys.sort()
            for key in keys:
                file.write("%s = %s\n" % (key, items[key]))
            
    def save(self):
        if not self.path:
            print "WARNING: no existing Hatari configuration to modify, saving canceled"
            return
        if not self.changed:
            print "No configuration changes to save, skipping"
            return            
        file = open(self.path, "w")
        if file:
            self.write(file)
            print "Saved Hatari configuration file:", self.path
        else:
            print "ERROR: opening '%s' for saving failed" % self.path

    def is_changed(self):
        return self.changed
