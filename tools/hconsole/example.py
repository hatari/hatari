#!/usr/bin/env python
#
# This is an example of scripting hatari using hconsole
#
# Run with (using correct EmuTOS path):
#   PATH=../../build/src/:$PATH ./example.py --tos etos512k.img
# Or if Hatari and hconsole are installed to the system:
#   /usr/share/hatari/hconsole/example.py --tos etos512k.img

import hconsole, os, sys

# path for this script
path = os.path.dirname(sys.argv[0])
# current work directory
cwd = os.path.abspath(os.path.curdir)

# shortcuts to hconsole stuff
#
# GEMDOS emulation dir is given because without
# a disk, EmuTOS console invocation is ~8s
main = hconsole.Main(sys.argv + ["."])
code = hconsole.Scancode

# execute commands from external file in current directory
main.script(path + "/example-commands")

# define shortcut functions for entering specific key presses
def backspace():
    main.run("keypress %s" % code.Backspace)
def enter():
    main.run("keypress %s" % code.Return)

# loop removing some of previously script output
for i in range(25):
    backspace()
enter()

# output some text to EmuTOS console in a loop
for i in range(3):
    main.run("text echo Welcome to 'hconsole'")
    enter()

# ask Hatari debugger to parse breakpoint etc commands from file
main.run("parse %s/example-debugger" % path)

# hit a Getdrv() breakpoint when EmuTOS prompt is redrawn
enter()

# wait few secs and kill Hatari
main.run("sleep 3")
main.run("kill")
