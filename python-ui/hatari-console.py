#!/usr/bin/env python
#
# Hatari console:
# Allows using Hatari shortcuts from console and changing Hatari
# command line options (even ones you cannot change from the UI)
# while Hatari is running.
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
    controlpath = "/tmp/hatari-console.socket"
    hataribin = "hatari"

    def __init__(self, args = None):
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
        if not self.run(args):
            print "ERROR: failed to run Hatari"
            sys.exit(1)
        
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
    
    def run(self, args):
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
            print "WAIT hatari to connect to control socket...",
            (self.control, addr) = self.server.accept()
            print "connected!"
            return self.control
        else:
            # child runs Hatari
            allargs = [self.hataribin, "--control-socket", self.controlpath] + args
            print "RUN:", allargs
            os.execvp(self.hataribin, allargs)

    def get_control(self):
        return self.control

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
class CommandInput():
    historysize = 99
    
    def __init__(self, commands):
        readline.set_history_length(self.historysize)
        readline.parse_and_bind("tab: complete")
        readline.set_completer_delims(" \t\r\n")
        readline.set_completer(self.complete)
        self.commands = commands
    
    def complete(self, text, state):
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


# update with: hatari -h|grep -- --|sed 's/^ *\(--[^ ]*\).*$/    "\1",/'
option_tokens = [
    "--help",
    "--version",
    "--confirm-quit",
    "--configfile",
    "--fast-forward",
    "--mono",
    "--monitor",
    "--fullscreen",
    "--window",
    "--zoom",
    "--frameskips",
    "--borders",
    "--spec512",
    "--bpp",
    "--vdi-planes",
    "--vdi-width",
    "--vdi-height",
    "--joystick",
    "--printer",
    "--midi",
    "--rs232",
    "--harddrive",
    "--acsi",
    "--ide",
    "--slowfdc",
    "--memsize",
    "--tos",
    "--cartridge",
    "--memstate",
    "--cpulevel",
    "--cpuclock",
    "--compatible",
    "--machine",
    "--blitter",
    "--dsp",
    "--sound",
    "--keymap",
    "--debug",
    "--bios-intercept",
    "--trace",
    "--trace-file",
    "--log-file",
    "--log-level",
    "--alert-level"
]
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
hatari = Hatari(sys.argv[1:])
process_tokens = {
    "pause": hatari.pause,
    "unpause": hatari.unpause,
}
control = hatari.get_control()

print "************************************************************"
print "* Use the TAB key to see all the available Hatari commands *"
print "************************************************************"
command = CommandInput(option_tokens + shortcut_tokens + process_tokens.keys())

while 1:
    line = command.loop().strip()
    if not hatari.is_running():
        print "Exiting as there's no Hatari (anymore)..."
        sys.exit(0)
    if not line:
        continue
    if line in process_tokens:
        process_tokens[line]()
    elif line in shortcut_tokens:
        control.send("hatari-shortcut " + line)
    elif line in option_tokens:
        control.send("hatari-option " + line)
    else:
        print "ERROR: unknown command"
