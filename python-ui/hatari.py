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
    
    def run_embedded(self, window):
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
            env = self.get_env(window)
            os.execvpe("hatari", args, env)

    def get_env(self, window):
        if sys.platform == 'win32':
            win_id = window.handle
        else:
            win_id = window.xid
        env = os.environ
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
    changed = False
    
    def __init__(self):
        print "TODO: load Hatari configuration file"
        # TODO: remove this once testing is done
        self.changed = True

    def is_changed(self):
        return self.changed

    def save(self):
        print "TODO: save Hatari configuration file"
