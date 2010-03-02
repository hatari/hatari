#!/usr/bin/env python
#
# Hatari console:
# Allows using Hatari shortcuts & debugger, changing paths, toggling
# devices and changing Hatari command line options (even for things you
# cannot change from the UI) from the console while Hatari is running.
#
# Copyright (C) 2008-2010 by Eero Tamminen <eerot at berlios>
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
import readline

# running Hatari instance
class Hatari:
    controlpath = "/tmp/hatari-console-" + str(os.getpid()) + ".socket"
    hataribin = "hatari"

    def __init__(self, args = None):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        self._assert_hatari_compatibility()
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

    def _assert_hatari_compatibility(self):
        for line in os.popen(self.hataribin + " -h").readlines():
            if line.find("--control-socket") >= 0:
                return
        print "ERROR: Hatari not found or it doesn't support the required --control-socket option!"
        sys.exit(-1)
        
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
            print "->", msg
            self.control.sendall(msg + "\n")
            # KLUDGE: wait so that Hatari output comes before next prompt
            time.sleep(0.2)
            return True
        else:
            print "ERROR: no Hatari (control socket)"
            return False
        
    def change_option(self, option):
        return self.send_message("hatari-option %s" % option)

    def trigger_shortcut(self, shortcut):
        return self.send_message("hatari-shortcut %s" % shortcut)

    def insert_event(self, event):
        return self.send_message("hatari-event %s" % event)

    def debug_command(self, cmd):
        return self.send_message("hatari-debug %s" % cmd)
    
    def change_path(self, path):
        return self.send_message("hatari-path %s" % path)
    
    def toggle_device(self, device):
        return self.send_message("hatari-toggle %s" % device)

    def pause(self):
        return self.send_message("hatari-stop")

    def unpause(self):
        return self.send_message("hatari-cont")
        
    def stop(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            print "killed hatari with PID %d" % self.pid
            self.control = None
            self.pid = 0


# command line parsing with readline
class CommandInput:
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
    "--grab",
    "--zoom",
    "--frameskips",
    "--borders",
    "--statusbar",
    "--drive-led",
    "--spec512",
    "--bpp",
    "--vdi",
    "--vdi-planes",
    "--vdi-width",
    "--vdi-height",
    "--avirecord",
    "--avi-vcodec",
    "--avi-fps",
    "--avi-crop",
    "--avi-file",
    "--joy0",
    "--joy1",
    "--joy2",
    "--joy3",
    "--joy4",
    "--joy5",
    "--joystick",
    "--printer",
    "--midi-in",
    "--midi-out",
    "--rs232-in",
    "--rs232-out",
    "--disk-a",
    "--disk-b",
    "--slowfdc",
    "--harddrive",
    "--mount-changes",
    "--acsi",
    "--ide-master",
    "--ide-slave",
    "--memsize",
    "--tos",
    "--cartridge",
    "--memstate",
    "--cpulevel",
    "--cpuclock",
    "--compatible",
    "--machine",
    "--blitter",
    "--timer-d",
    "--dsp",
    "--sound",
    "--keymap",
    "--debug",
    "--bios-intercept",
    "--trace",
    "--trace-file",
    "--parse",
    "--log-file",
    "--log-level",
    "--alert-level",
    "--run-vbls"
]
shortcut_tokens = [
    "mousemode",
    "coldreset",
    "warmreset",
    "screenshot",
    "bosskey",
    "recanim",
    "recsound",
    "savemem"
]
event_tokens = [
    "doubleclick",
    "rightpress",
    "rightrelease",
    "keypress",
    "keyrelease"
]
device_tokens = [
    "printer",
    "rs232",
    "midi",
]
path_tokens = [
    "memauto",
    "memsave",
    "midiout",
    "printout",
    "soundout",
    "rs232in",
    "rs232out"
]
# use the long variants of the commands for clarity
debugger_tokens = [
    "address",
    "breakpoint",
    "cd",
    "cont",
    "cpureg",
    "disasm",
    "dspaddress",
    "dspbreak",
    "dspcont",
    "dspdisasm",
    "dspmemdump",
    "dspreg",
    "dspsymbols",
    "evaluate",
    "exec",
    "help",
    "info",
    "loadbin",
    "lock",
    "logfile",
    "memdump",
    "memwrite",
    "parse",
    "savebin",
    "setopt",
    "stateload",
    "statesave",
    "symbols",
    "trace"
]


def list_items(title, items):
    print "\n%s:" % title
    for item in items:
        print "*", item

def show_help():
    print """
Hatari-console help
-------------------

Hatari-console allows you to control Hatari through its control socket
from the provided console prompt, while Hatari is running.  All control
commands support TAB completion on their names and options.

The supported control facilities are:"""
    list_items("Command line options", option_tokens)
    list_items("Keyboard shortcuts", shortcut_tokens)
    list_items("Event invocation", event_tokens)
    list_items("Device toggling", device_tokens)
    list_items("Path setting", path_tokens)
    list_items("Debugger commands", debugger_tokens)
    print """
and commands to "pause", "unpause" and "quit" Hatari.

For command line options you can see further help with "--help"
and for debugger with "help".  Some other facilities may give
help if you give them invalid input.
"""


def main():
    hatari = Hatari(sys.argv[1:])
    process_tokens = {
        "pause": hatari.pause,
        "unpause": hatari.unpause,
        "quit": hatari.stop
    }
    
    print "****************************************************************"
    print "* To see available commands, use the TAB key or 'console-help' *"
    print "****************************************************************"
    tokens = ["console-help"]
    for items in [option_tokens, shortcut_tokens, event_tokens,
            debugger_tokens, device_tokens, path_tokens,
            process_tokens.keys()]:
        for token in items:
            if token in tokens:
                print "ERROR: token '%s' already in tokens" % token
                sys.exit(1)
        tokens += items
    command = CommandInput(tokens)
    
    while 1:
        line = command.loop().strip()
        if not hatari.is_running():
            print "Exiting as there's no Hatari (anymore)..."
            sys.exit(0)
        if not line:
            continue
        first = line.split(" ")[0]
        # multiple items
        if first in event_tokens:
            hatari.insert_event(line)
        elif first in debugger_tokens:
            hatari.debug_command(line)
        elif first in option_tokens:
            hatari.change_option(line)
        elif first in path_tokens:
            hatari.change_path(line)
        # single item
        elif line in device_tokens:
            hatari.toggle_device(line)
        elif line in shortcut_tokens:
            hatari.trigger_shortcut(line)
        elif line in process_tokens:
            process_tokens[line]()
        elif line == "console-help":
            show_help()
        else:
            print "ERROR: unknown hatari-console command:", line


if __name__ == "__main__":
    main()
