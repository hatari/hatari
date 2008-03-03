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

# running Hatari instance, singleton
class Hatari():
    pid = 0  # Hatari emulator PID, zero if not running
    paused = False
    
    def __init__(self):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)

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
    
    def run(self, parent_win = None):
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
            args = self.get_args()
            env = self.get_env(parent_win)
            os.execvpe("hatari", args, env)

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

    def get_args(self):
        print "TODO: get the Hatari cmdline options from configuration"
        args = ("hatari", "-m", "-z", "2")
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


# current Hatari configuration, singleton
class Config():
    confpath = None # existing Hatari configuration
    changed = False # whether it's changed

    def __init__(self):
        self.confpath = self.get_confpath()
        if self.confpath:
            print "TODO: load Hatari configuration file:", self.confpath
        else:
            print "Hatari configuration file missing"
        # TODO: remove this once testing is done
        self.changed = True

    def get_confpath(self):
        # hatari.cfg can be in home or current work dir
        for path in (os.getenv("HOME"), os.getcwd()):
            if path:
                confpath = self.check_confpath(path)
                if confpath:
                    return confpath
        return None

    def check_confpath(self, path):
        # check path/.hatari/hatari.cfg, path/hatari.cfg
        path += os.path.sep
        testpath = path + ".hatari" + os.path.sep + "hatari.cfg"
        if os.path.exists(testpath):
            return testpath
        testpath = path + "hatari.cfg"
        if os.path.exists(testpath):
            return testpath
        return None

    def is_changed(self):
        return self.changed

    def write(self, confpath):
        print "TODO: save Hatari configuration file:", confpath
            
    def save(self):
        if self.confpath:
            self.write(self.confpath)
        else:
            print "WARNING: no existing Hatari configuration to modify, saving canceled"
