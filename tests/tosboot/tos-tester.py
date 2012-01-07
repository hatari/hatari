#!/usr/bin/env python
#
# This script tests that all given TOS image files boot under
# Hatari with several different HW configurations and saves
# verification screenshots into current directory.
#
# If Hatari and hconsole are installed to the system and
# you want to test those, you need to tell where hconsole
# is installed by setting PYTHONPATH correctly, like this:
#   export PYTHONPATH=/usr/share/hatari/hconsole
#   ./tos-tester.py <TOS images>
# 
# If you want to test latest versions of hconsole and Hatari,
# you need to set also PATH to point to your Hatari binary
# directory, like this:
#   export PYTHONPATH=../../tools/hconsole
#   PATH=../../build/src:$PATH ./tos-tester.py <TOS images>
#
# Copyright (C) 2012 by Eero Tamminen <oak at helsinkinet fi>
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

import hconsole, os, sys

def warning(msg):
    sys.stderr.write("WARNING: %s\n" % msg)
def error_exit(msg):
    sys.stderr.write("""
usage: %s <TOS image files>

Boots the given TOS versions under Hatari with a selection
of machine and monitor types and memory sizes supported by
the given TOS version.

Verification screenshot is taken of the booted TOS desktop
before proceeding to booting the next combination.

Screenshot name indicates the used combination, for example:
        etos512k-falcon-rgb-14M.png
        etos512k-st-mono-1M.png

ERROR: %s!
""" % (os.path.basename(sys.argv[0]), msg))
    sys.exit(1)


class TOS:
    def __init__(self, argv):
        self.images = []
        if len(argv) < 1:
            error_exit("no TOS image files given")
        for img in argv:
            self.add_image(img)
        if not self.images:
            error_exit("no (valid) TOS image files given")
    
    def image_machines(self, img):
        (version, is_etos) = self.image_version(img)
        if is_etos:
            if version > 0x200:
                return (is_etos, ("st", "ste", "tt", "falcon"))
            else:
                return (is_etos, ("st", "ste", "tt"))
        elif version < 0x160:
            return (is_etos, ("st",))
        elif version < 0x200:
            return (is_etos, ("ste",))
        elif version < 0x300:
            return (is_etos, ("st", "ste", "tt"))
        elif version < 0x400:
            return (is_etos, ("tt",))
        else:
            return (is_etos, ("falcon",))
    
    def image_version(self, img):
        "return tuple of (TOSversion, IsEmuTOS)"
        f = open(img)
        f.seek(0x2, 0)
        version = (ord(f.read(1)) << 8) + ord(f.read(1))
        f.seek(0x2C, 0)
        etos = f.read(4)
        return (version, etos == "ETOS")
    
    def add_image(self, img):
        "add valid TOS images"
        if not os.path.isfile(img):
            warning("'%s' isn't a file")
            return
        size = os.stat(img).st_size
        tossizes = (196608, 262144, 524288)
        if size not in tossizes:
            warning("image '%s' size not one of TOS sizes %s" % (img, repr(tossizes)))
            return
        basename = os.path.basename(img)
        (version, is_etos) = self.image_version(img)
        if is_etos:
            print "%s is EmuTOS v%x" % (basename, version)
        elif version >= 0x100 and version < 0x500:
            print "%s is normal TOS v%x" % (basename, version)
        else:
            warning("'%s' with TOS version 0x%x isn't valid" % (basename, version))
            return
        self.images.append(img)

    def files(self):
        return self.images


class Tester:
    # dummy config file to force suitable default options
    dummycfg = "dummy.cfg"
    defaults = [sys.argv[0], "--configfile", dummycfg]
    hddir = "gemdos"
    
    def __init__(self):
        self.images = TOS(sys.argv[1:])
        # dummy configuration to avoid user's own config,
        # get rid of the dialogs, disable GEMDOS emu by default,
        # use empty blank disk to avoid TOS error when no disks
        # and get rid of statusbar and borders in TOS screenshots
        # to make them smaller & more consistent
        dummy = open(self.dummycfg, "w")
        dummy.write("[Log]\nnAlertDlgLogLevel = 0\nbConfirmQuit = FALSE\n")
        dummy.write("[HardDisk]\nbUseHardDiskDirectory = FALSE\n")
        dummy.write("[Floppy]\nszDiskAFileName = blank-a.st.gz\n")
        dummy.write("[Screen]\nbCrop = TRUE\nbAllowOverscan=FALSE\n")
        dummy.close()
        # directory for GEMDOS emu testing
        if not os.path.isdir(self.hddir):
            os.mkdir(self.hddir)
        # remove left over screenshots
        if os.path.isfile("grab0001.png"):
            os.remove("grab0001.png")
        if os.path.isfile("grab0001.bmp"):
            os.remove("grab0001.bmp")

    def test(self, identity, testargs, bootwait, deskwait):
        sys.argv = self.defaults + testargs
        instance = hconsole.Main()
        # pass memory test
        instance.run("sleep %d" % bootwait)
        instance.run("keypress %s" % hconsole.Scancode.Space)
        # wait until in desktop, TOS3 is slower in color
        instance.run("sleep %d" % deskwait)
        # screenshot of desktop
        instance.run("screenshot")
        if os.path.isfile("grab0001.png"):
            os.rename("grab0001.png", identity+".png")
        elif os.path.isfile("grab0001.bmp"):
            os.rename("grab0001.bmp", identity+".bmp")
        else:
            warning("failed to locate screenshot grab0001.{png,bmp}")
        # get rid of this Hatari instance
        instance.run("kill")

    def run(self):
        for tos in self.images.files():

            name = os.path.basename(tos)
            name = name[:name.rfind('.')]
            print
            print "***** TESTING: %s *****" % name
            print
            
            (is_etos, machines) = self.images.image_machines(tos)
            for machine in machines:

                if machine in ("st", "ste"):
                    bootwait = 1
                    deskwait = 3
                    if is_etos:
                        # EmuTOS is slower than TOS 1.x
                        deskwait += 2
                    memories = (1, 4) # (0, 1, 2, 4)
                    if machine == "st":
                        monitors = ("tv", "mono", "vdi1", "vdi4")
                    else:
                        monitors = ("rgb", "mono", "vdi1", "vdi4")
                else:
                    bootwait = 2
                    # e.g. TOS3 is quite slow in color modes
                    deskwait = 5
                    memories = (1,14) # (1, 4, 8, 14)
                    if machine == "tt" or is_etos:
                        monitors = ("rgb", "mono", "vdi1", "vdi4")
                    else:
                        # VDI modes don't work with TOS4
                        monitors = ("rgb", "vga", "mono")

                for monitor in monitors:
                    for memory in memories:
                        # e.g. TOS4 is slower with more mem
                        bootwait += memory//8
                        deskwait += memory//8
                        for gemdos in (False, True):
                            identity = "%s-%s-%s-%sM" % (name, machine, monitor, memory)
                            testargs = ["--memsize", str(memory), "--machine", machine, "--tos", tos]
                            if monitor[:3] == "vdi":
                                planes = monitor[-1]
                                if planes == "1":
                                    testargs += ["--vdi-width", "640", "--vdi-height", "480", "--vdi-planes", planes]
                                else:
                                    testargs += ["--vdi-width", "320", "--vdi-height", "240", "--vdi-planes", planes]
                            else:
                                testargs += ["--monitor", monitor]
                            if gemdos:
                                identity += "-gemdos"
                                testargs += ["--harddrive", self.hddir]
                            self.test(identity, testargs, bootwait, deskwait)



if __name__ == "__main__":
    tests = Tester()
    tests.run()
