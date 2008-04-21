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
import socket
import readline

# running Hatari instance
class Hatari():
    controlpath = "/tmp/hatari-ui.socket"
    hataribin = "hatari"

    def __init__(self):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        if os.path.exists(self.controlpath):
            os.unlink(self.controlpath)
        self.server.bind(self.controlpath)
        self.server.listen(1)
        self.control = None
        self.paused = False
        self.pid = 0
        
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
    
    def run(self):
        if self.control:
            print "ERROR: Hatari is already running, stop it first"
            return
        pid = os.fork()
        if pid < 0:
            print "ERROR: fork()ing Hatari failed!"
            return
        if pid:
            # in parent
            self.pid = pid
            print "WAIT hatari to connection to control socket"
            (self.control, addr) = self.server.accept()
            return self.control
        else:
            # child runs Hatari
            args = (self.hataribin, "--control-socket", self.controlpath)
            print "RUN:", args
            os.execvp(self.hataribin, args)

    def pause(self):
        if self.pid and not self.paused:
            os.kill(self.pid, signal.SIGSTOP)
            print "paused hatari with PID %d" % self.pid
            self.paused = True
    
    def unpause(self):
        if self.pid and self.paused:
            os.kill(self.pid, signal.SIGCONT)
            print "continued hatari with PID %d" % self.pid
            self.paused = False
        
    def stop(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.pid
            self.control = None
            self.pid = 0


# command line parsing with readline
class Command():
    historylen = 99
    
    def __init__(self, commands):
        readline.set_history_length(self.historylen)
        #readline.insert_text()
        readline.parse_and_bind("tab: complete")
        readline.set_completer(self._complete)
        self.commands = commands
    
    def _complete(self, text, state):
        idx = 0
        for command in self.commands:
            if command.startswith(text):
                idx += 1
                if idx > state:
                    return command
        #print "text: '%s', state '%d'" % (text, state)
    
    def loop(self):
        try:
            line = raw_input("hatari-command: ")
            return line
        except EOFError:
            return ""

control = None
hatari = Hatari()
process_tokens = {
    "pause": hatari.pause,
    "unpause": hatari.unpause,
}
shortcut_tokens = [
    "mousemode",
    "coldreset",
    "warmreset",
    "screenshot",
    "bosskey",
    "recanim",
    "recsound",
    "debug",
    "quit"
]
command = Command(["run"] + process_tokens.keys() + shortcut_tokens)
while 1:
    line = command.loop().strip()
    if not line:
        continue
    if not hatari.is_running():
        if line == "run":
            control = hatari.run()
        else:
            print "ERROR: 'run' Hatari first"
            print "After that you can try commands:"
            for key in process_tokens.keys() + shortcut_tokens:
                print "-", key
            control = None
        continue
    if line in process_tokens:
        tokens[line]()
    elif line in shortcut_tokens:
        control.send("hatari-shortcut " + line)
    else:
        print "ERROR: unknown command"

