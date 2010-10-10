#!/usr/bin/env python
#
# This is an example of scripting hatari using hconsole
#
# Run with:
# ./example.py --tos etos512k.img

import hconsole
run = hconsole.Main().run

# wait for TOS to boot
run("sleep 3")

# Start EmuTOS console with ^Z:
# Control down, press Z, Control up
# (Control key scancode is 29)
run("keydown 29")
run("keypress Z")
run("keyup 29")

# output some text to EmuTOS console in a loop
for i in range(5):
    run("text echo Welcome to 'hatariconsole'")
    # press Enter
    run("keypress 28")

run("sleep 3")
run("quit")
