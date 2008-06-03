#!/usr/bin/env python
#
# Hatari console:
# Allows using Hatari shortcuts, debugger and changing Hatari
# command line options (even ones you cannot change from the UI)
# from the console while Hatari is running.
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
    controlpath = "/tmp/hatari-console-" + str(os.getpid()) + ".socket"
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
            os.waitpid(self.pid, os.WNOHANG)
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
        
    def stop(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.pid
            self.control = None
            self.pid = 0


# command line parsing with readline
class CommandInput():
    prompt = "hatari-command: "
    historysize = 99
    
    def __init__(self, commands):
        readline.set_history_length(self.historysize)
        readline.parse_and_bind("tab: complete")
        readline.set_completer_delims(" \t\r\n")
        readline.set_completer(self.complete)
        self.commands = commands
    
    def complete(self, text, state):
        idx = 0
        #print "text: '%s', state '%d'" % (text, state)
        for cmd in self.commands:
            if cmd.startswith(text):
                idx += 1
                if idx > state:
                    return cmd
    
    def loop(self):
        try:
            rawline = raw_input(self.prompt)
            return rawline
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
event_tokens = [
    "doubleclick",
    "rightpress",
    "rightrelease",
    "keypress",
    "keyrelease"
]
debugger_tokens = [
    "r",
    "d",
    "m",
    "f",
    "w",
    "l",
    "s",
    "h"
]

def main():
    hatari = Hatari(sys.argv[1:])
    process_tokens = {
        "pause": hatari.pause,
        "unpause": hatari.unpause,
        "quit": hatari.stop
    }
    
    print "************************************************************"
    print "* Use the TAB key to see all the available Hatari commands *"
    print "************************************************************"
    tokens = option_tokens + shortcut_tokens + event_tokens \
             + debugger_tokens + process_tokens.keys()
    command = CommandInput(tokens)
    
    while 1:
        line = command.loop().strip()
        if not hatari.is_running():
            print "Exiting as there's no Hatari (anymore)..."
            sys.exit(0)
        if not line:
            continue
        first = line.split(" ")[0]
        if line in process_tokens:
            process_tokens[line]()
        elif line in shortcut_tokens:
            hatari.trigger_shortcut(line)
        elif first in event_tokens:
            hatari.insert_event(line)
        elif first in debugger_tokens:
            hatari.debug_command(line)
        elif first in option_tokens:
            hatari.change_option(line)
        else:
            print "ERROR: unknown command:", line


if __name__ == "__main__":
    main()
