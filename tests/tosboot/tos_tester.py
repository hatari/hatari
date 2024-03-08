#!/usr/bin/env python3
#
# Copyright (C) 2012-2022 by Eero Tamminen <oak at helsinkinet fi>
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
"""
Tester boots the given TOS versions under Hatari with all the possible
combinations of the given machine HW configuration options, that are
supported by the tested TOS version.

Verification screenshot is taken at the end of each boot before
proceeding to testing of the next combination.  Screenshot name
indicates the used combination, for example:
        etos1024k-falcon-rgb-gemdos-14M.png
        etos1024k-st-mono-floppy-1M.png


NOTE: If you want to test the latest, uninstalled version of Hatari,
you need to set PATH to point to your Hatari binary directory, like
this:
	PATH=../../build/src:$PATH tos_tester.py <TOS images>

If hconsole isn't installed to one of the standard locations (under
/usr or /usr/local), or you don't run this from within Hatari sources,
you also need to specify hconsole.py location with:
	export PYTHONPATH=/path/to/hconsole
"""

import getopt, os, signal, select, sys, time

def add_hconsole_paths():
    "add most likely hconsole locations to module import path"
    # prefer the devel version in Hatari sources, if it's found
    subdirs = len(os.path.abspath(os.curdir).split(os.path.sep))-1
    for level in range(subdirs):
        f = level*(".." + os.path.sep) + "tools/hconsole/hconsole.py"
        if os.path.isfile(f):
            f = os.path.dirname(f)
            sys.path.append(f)
            print("Added local hconsole path: %s" % f)
            break
    sys.path += ["/usr/local/share/hatari/hconsole",
                 "/usr/share/hatari/hconsole"]

add_hconsole_paths()
import hconsole


def warning(msg):
    "output warning message"
    sys.stderr.write("WARNING: %s\n" % msg)

def error_exit(msg):
    "output error and exit"
    sys.stderr.write("ERROR: %s\n" % msg)
    sys.exit(1)


# -----------------------------------------------
class TOS:
    "class for TOS image information"
    # objects have members:
    # - path (string),  given TOS image file path/name
    # - name (string),  filename with path and extension stripped
    # - size (int),     image file size, in kB
    # - etos (bool),    is EmuTOS?
    # - version (int),  TOS version
    # - memwait (int),  how many secs to wait before memcheck key press
    # - fullwait (int), after which time safe to conclude boot to have failed
    # - machines (tuple of strings), which Atari machines this TOS supports

    def __init__(self, path):
        self.path, self.size, self.name = self._add_file(path)
        self.version, self.etos = self._add_version()
        self.memwait, self.fullwait, self.machines = self._add_info()


    def _add_file(self, img):
        "get TOS file size and basename for 'img'"
        if not os.path.isfile(img):
            raise AssertionError("'%s' given as TOS image isn't a file" % img)

        size = os.stat(img).st_size
        if size % 1024:
            raise AssertionError("image '%s' has invalid size of %g KB" % (img, size/1024.))
        size /= 1024
        if size not in (192, 256, 512, 1024):
            raise AssertionError("image '%s' size (%d KB) is not one of TOS sizes" % (img, size))

        name = os.path.basename(img)
        name = name[:name.rfind('.')]
        return (img, size, name)


    def _add_version(self):
        "get TOS version and whether it's EmuTOS & supports GEMDOS HD"
        f = open(self.path, 'rb')
        f.seek(0x2, 0)
        version = (ord(f.read(1)) << 8) + ord(f.read(1))
        # older TOS versions don't support autostarting
        # programs from GEMDOS HD dir with *.INF files
        f.seek(0x2C, 0)
        etos = (f.read(4) == b"ETOS")
        return (version, etos)


    def _add_info(self):
        "add TOS version specific info of supported machines etc, return timeouts & supported machines"
        name, size, version = self.name, self.size, self.version

        if self.etos:
            # EmuTOS 1024/512k, 256k and 192k versions have different machine support
            if size in (512, 1024):
                # startup screen on falcon 14MB is really slow
                info = (5, 10, ("st", "megast", "ste", "megaste", "tt", "falcon"))
            elif size == 256:
                info = (2, 8, ("st", "megast", "ste", "megaste"))
            elif size == 192:
                info = (0, 6, ("st", "megast"))
            else:
                raise AssertionError("'%s' image size %dkB isn't valid for EmuTOS" % (name, size))
        # https://en.wikipedia.org/wiki/Atari_TOS
        elif version <= 0x100:
            # boots up really slow with 4MB
            info = (0, 20, ("st",))
        elif version <= 0x104:
            info = (0, 12, ("st", "megast"))
        elif version <= 0x162:
            info = (0, 20, ("ste",))
        elif version < 0x206:
            # TOS v2.x are slower with VDI mode than others
            info = (3, 14, ("ste", "megaste"))
        elif version == 0x206:
            # ST support added to TOS 2.x only after 2.05
            info = (3, 14, ("st", "megast", "ste", "megaste"))
        elif version <= 0x306:
            # MMU slowdown is taken care of in prepare_test()
            info = (3, 20, ("tt",))
        elif version <= 0x404:
            # no-IDE scan slowdown is taken care of in prepare_test()
            info = (3, 28, ("falcon",))
        else:
            raise AssertionError("Unknown '%s' TOS version 0x%x" % (name, version))

        if self.etos:
            print("%s is %dkB EmuTOS declaring itself TOS v%x (wait startup: %ds, rest: %ds)" % (name, size, version, info[0], info[1]))
        else:
            print("%s is normal TOS v%x (wait memcheck: %ds, rest: %ds)" % (name, version, info[0], info[1]))
        # 0: whether / how long to wait to dismiss memory test
        # 1: how long to wait until concluding test failed
        # 2: list of machines supported by this TOS version
        return info

    def supports_gemdos_hd(self):
        "whether TOS version supports Hatari's GEMDOS HD emulation"
        return self.version >= 0x0104

    def supports_hdinterface(self, hdinterface):
        "whether TOS version supports monitor that is valid for given machine"
        # EmuTOS doesn't require drivers to access DOS formatted disks
        if self.etos:
            # IDE support is in EmuTOS since 0.9.0, SCSI since 0.9.10
            if hdinterface != "acsi" and self.size < 512:
                return False
            return True
        # As ACSI (big endian) and IDE (little endian) images would require
        # diffent binary drivers on them and it's not possible to generate
        # such images automatically, testing ACSI & IDE images for normal
        # TOS isn't support.
        #
        # (And even with a driver, only TOS 4.x supports IDE.)
        print("NOTE: '%s' hard disk tests are supported only for EmuTOS" % hdinterface)
        return False

    def supports_monitor(self, monitortype, machine):
        "whether TOS version supports monitor that is valid for given machine"
        # other monitor types valid for the machine are
        # valid also for TOS that works on it
        if monitortype.startswith("vdi"):
            # VDI mode doesn't work properly until TOS v1.02
            if self.version < 0x102:
                return False
            # sensible sized VDI modes don't work with TOS4
            # (nor make sense with its Videl expander support)
            if self.version >= 0x400:
                return False
            if self.etos:
                # smallest EmuTOS image doesn't have any Falcon support
                if machine == "falcon" and self.size == 192:
                    return False
            # 2-plane modes don't work properly with real TOS
            elif monitortype.endswith("2"):
                return False
        return True

    def supports_32bit_addressing(self, disk):
        "whether TOS version supports 32-bit addressing"
        if self.etos or (self.version >= 0x300 and self.version < 0x400):
            return True
        # Hatari patches TOS v4 for 32-bit support, but TOS v4 floppy access doesn't
        # work with TT-RAM as there's no _FRB cookie pointing to 64K ST-RAM DMA buffer
        if self.version >= 0x400 and disk != 'floppy':
            return True
        return False


# -----------------------------------------------
def validate(args, full):
    "return set of members not in the full set and given args"
    return (set(args).difference(full), args)

class Config:
    "Test configuration and validator class"
    # full set of possible options
    all_disks = ("floppy", "gemdos", "acsi", "ide", "scsi")
    all_graphics = ("mono", "rgb", "vga", "tv", "vdi1", "vdi2", "vdi4")
    all_machines = ("st", "megast", "ste", "megaste", "tt", "falcon")
    all_memsizes = (0, 1, 2, 4, 8, 10, 14)

    # defaults
    fast = False
    opts = []
    bools = []
    disks = ("floppy", "gemdos", "scsi")
    graphics = ("mono", "rgb", "vga", "vdi1", "vdi4")
    machines = ("st", "ste", "megaste", "tt", "falcon")
    memsizes = (0, 4, 14)
    ttrams = (0, 32)

    def __init__(self, argv):
        longopts = ["bool=", "disks=", "fast", "graphics=", "help", "machines=", "memsizes=", "opts=", "ttrams="]
        try:
            opts, paths = getopt.gnu_getopt(argv[1:], "b:d:fg:hm:s:o:t:", longopts)
        except getopt.GetoptError as error:
            self.usage(error)
        self.handle_options(opts)
        self.images = self.check_images(paths)
        print("\nTest configurations:")
        print("- machines = %s" % (self.machines,))
        print("- graphics = %s" % (self.graphics,))
        print("- disks = %s" % (self.disks,))
        print("- RAM = %s" % (self.memsizes,))
        print("- TTRAM = %s" % (self.ttrams,))
        print("- bools = %s" % (self.bools,))
        print("- fixed = '%s'\n" % ' '.join(self.opts))


    def check_images(self, paths):
        "validate given TOS images"
        images = []
        for img in paths:
            try:
                images.append(TOS(img))
            except AssertionError as msg:
                self.usage(msg)
        if not images:
            self.usage("no TOS image files given")
        return images


    def handle_options(self, opts):
        "parse command line options"
        unknown = None
        for opt, arg in opts:
            args = arg.split(",")
            if opt in ("-h", "--help"):
                self.usage()
            if opt in ("-f", "--fast"):
                self.fast = True
            elif opt in ("-b", "--bool"):
                self.bools = args
            elif opt in ("-o", "--opts"):
                self.opts = arg.split()
            elif opt in ("-d", "--disks"):
                unknown, self.disks = validate(args, self.all_disks)
            elif opt in ("-g", "--graphics"):
                unknown, self.graphics = validate(args, self.all_graphics)
            elif opt in ("-m", "--machines"):
                unknown, self.machines = validate(args, self.all_machines)
            elif opt in ("-s", "--memsizes"):
                try:
                    args = [int(i) for i in args]
                except ValueError:
                    self.usage("non-numeric memory sizes: %s" % arg)
                unknown, self.memsizes = validate(args, self.all_memsizes)
            elif opt in ("-t", "--ttrams"):
                try:
                    args = [int(i) for i in args]
                except ValueError:
                    self.usage("non-numeric TT-RAM sizes: %s" % arg)
                for ram in args:
                    if ram < 0 or ram > 1024:
                        self.usage("invalid TT-RAM (0-1024) size: %d" % ram)
                self.ttrams = args
            if unknown:
                self.usage("%s are invalid values for %s" % (list(unknown), opt))


    def usage(self, msg=None):
        "output program usage information"
        name = os.path.basename(sys.argv[0])
        # option value lists in directly CLI copy-pastable format
        disks, graphics, machines = [','.join(x) for x in
            (self.all_disks, self.all_graphics, self.all_machines)]
        memsizes = ','.join([str(x) for x in self.all_memsizes])
        print(__doc__)
        print("""
Usage: %s [options] <TOS image files>

Options:
\t-h, --help\tthis help
\t-f, --fast\tspeed up boot with less accurate emulation:
\t\t\t"--fast-forward yes --fast-boot yes --fastfdc yes --timer-d yes"
\t-d, --disks\t(%s)
\t-g, --graphics\t(%s)
\t-m, --machines\t(%s)
\t-s, --memsizes\t(%s)
\t-t, --ttrams\t(0-1024, in 4MiB steps)
\t-b, --bool\t(extra boolean Hatari options to test)
\t-o, --opts\t(hatari options to pass as-is)

Multiple values for an option need to be comma separated. If option
is given multiple times, last given value(s) are used. If some
option isn't given, default list of values will be used for that.

For example:
  %s \\
\t--disks gemdos \\
\t--machines st,tt \\
\t--memsizes 0,4,14 \\
\t--ttrams 0,32 \\
\t--graphics mono,rgb \\
\t--bool --compatible,--drive-b \\
\t--opts "--mmu on"
""" % (name, disks, graphics, machines, memsizes, name))
        if msg:
            print("ERROR: %s\n" % msg)
        sys.exit(1)


    def valid_disktype(self, machine, tos, disktype):
        "return whether given disk type is valid for given machine / TOS version"
        if disktype == "floppy":
            return True
        if disktype == "gemdos":
            return tos.supports_gemdos_hd()

        if machine in ("st", "megast", "ste", "megaste"):
            hdinterface = ("acsi",)
        elif machine == "tt":
            hdinterface = ("acsi", "scsi")
        elif machine == "falcon":
            hdinterface = ("ide", "scsi")
        else:
            raise AssertionError("unknown machine %s" % machine)

        if disktype in hdinterface:
            return tos.supports_hdinterface(disktype)
        return False

    def valid_monitortype(self, machine, tos, monitortype):
        "return whether given monitor type is valid for given machine / TOS version"
        if machine in ("st", "megast", "ste", "megaste"):
            monitors = ("mono", "rgb", "tv", "vdi1", "vdi2", "vdi4")
        elif machine == "tt":
            monitors = ("mono", "vga", "vdi1", "vdi2", "vdi4")
        elif machine == "falcon":
            monitors = ("mono", "rgb", "vga", "vdi1", "vdi2", "vdi4")
        else:
            raise AssertionError("unknown machine %s" % machine)
        if monitortype in monitors:
            return tos.supports_monitor(monitortype, machine)
        return False

    def valid_memsize(self, machine, memsize):
        "return True if given memory size is valid for given machine"
        # TT & MegaSTE can address only 10MB RAM due to VME, but
        # currently Hatari allows all RAM amounts on all HW
        if memsize > 10 and machine in ("megaste", "tt"):
            # TODO: return False when Hatari supports VME
            # (or disable VME when testing 14MB)
            return True # False
        return True

    def valid_ttram(self, machine, tos, ttram, disk):
        "return whether given TT-RAM size is valid for given machine"
        if ttram == 0:
            return True
        if machine in ("st", "megast", "ste", "megaste"):
            return False
        if machine in ("tt", "falcon"):
            if ttram < 0 or ttram > 1024:
                return False
            return tos.supports_32bit_addressing(disk)
        raise AssertionError("unknown machine %s" % machine)

    def validate_bools(self):
        "exit with error if given bool option is invalid"
        # Several bool options are left out of these lists, either because
        # they can be problematic for running of the tests themselves, or
        # they should have no impact on emulation, only emulator.
        #
        # Below ones should be potentially relevant ones to test
        # (EmuTOS supports NatFeats so it can have impact too)
        opts = (
            "--compatible", "--timer-d", "--fast-boot",
            "--natfeats", "--fastfdc", "--drive-b",
            "--cpu-exact", "--mmu", "--addr24", "--fpu-softfloat"
        )
        for option in self.bools:
            if option not in opts:
                error_exit("bool option '%s' not in relevant options set:\n\t%s" % (option, opts))

    def valid_bool(self, machine, option):
        "return True if given bool option is relevant to test"
        if option in ("--mmu", "--addr24") and machine not in ("tt", "falcon"):
            # MMU & 32-bit addressing are relevant only for 030
            return False
        return True

# -----------------------------------------------
def verify_file_match(srcfile, dstfile, identity):
    "return error string if sizes of given files don't match, and rename dstfile to identity"
    if not os.path.exists(dstfile):
        return "file '%s' missing" % dstfile
    dstsize = os.stat(dstfile).st_size
    srcsize = os.stat(srcfile).st_size
    if dstsize != srcsize:
        os.rename(dstfile, "%s.%s" % (dstfile, identity))
        return "file '%s' size %d doesn't match file '%s' size %d" % (srcfile, srcsize, dstfile, dstsize)
    return None

def verify_file_empty(filename, identity):
    "return error string if given file isn't empty, and rename file to identity"
    if not os.path.exists(filename):
        return "file '%s' missing" % filename
    size = os.stat(filename).st_size
    if size != 0:
        os.rename(filename, "%s.%s" % (filename, identity))
        return "file '%s' isn't empty (%d bytes)" % (filename, size)
    return None

def exit_if_missing(names):
    "exit if given (test input) file is missing"
    for name in names:
        if not os.path.exists(name):
            error_exit("test file '%s' missing")


# how long to wait for invoked Hatari to open FIFO (= MIDI output file)
FIFO_WAIT = 5

class Tester:
    "test driver class"
    output = "output" + os.path.sep
    report = output + "report.txt"
    # dummy Hatari config file to force suitable default options
    dummycfg  = "dummy.cfg"
    defaults  = [sys.argv[0], "--configfile", dummycfg]
    testprg   = "disk" + os.path.sep + "GEMDOS.PRG"
    textinput = "disk" + os.path.sep + "TEXT"
    textoutput= "disk" + os.path.sep + "TEST"
    printout  = output + "printer-out"
    serialout = output + "serial-out"
    fifofile  = output + "midi-out"
    # oldest TOS versions don't support GEMDOS HD or auto-starting,
    # they need to use floppy and boot test program from AUTO/
    bootauto  = "bootauto.st.gz"
    bootdesk  = "bootdesk.st.gz"
    floppyprg = "A:\\MINIMAL.PRG"
    # with EmuTOS, same image works for ACSI, SCSI and IDE testing,
    # whereas with real TOS, different images with different drivers
    # would be needed, potentially also for different machine types...
    hdimage   = "hd.img"
    hdprg     = "C:\\MINIMAL.PRG"
    results   = None

    def __init__(self):
        "test setup initialization"
        self.cleanup_all_files()
        self.create_config()
        self.create_files()
        signal.signal(signal.SIGALRM, self.alarm_handler)
        hatari = hconsole.Hatari(["--confirm-quit", "no"])
        if not hatari.winuae:
            error_exit("Hatari version available does not have WinAUE CPU core")
        hatari.kill_hatari()

    def alarm_handler(self, signum, dummy):
        "output error if (timer) signal came before passing current test stage"
        if signum == signal.SIGALRM:
            raise OSError("ERROR: timeout")
        else:
            print("ERROR: unknown signal %d received" % signum)
            raise AssertionError

    def create_config(self):
        "create Hatari default configuration file for testing"
        # override user's own config with default config to:
        # - get rid of the dialogs
        # - disable NatFeats
        # - disable fast boot options
        # - run as fast as possible (fast forward)
        # - use best CPU emulation options & 24-bit addressing
        # - disable ST blitter & (for now) MMU
        # - use dummy DSP & HW FPU types for speed
        #   (OS doesn't use them, it just checks for their presence)
        # - enable TOS patching and disable cartridge
        # - limit Videl zooming to same sizes as ST screen zooming
        # - don't warp mouse on resolution changes
        # - get rid of borders in TOS screenshots
        #   to make them smaller & more consistent
        # - disable GEMDOS emu by default
        # - disable GEMDOS HD write protection and host time use
        # - disable fast FDC & floppy write protection
        # - use empty floppy disk image to avoid TOS error when no disks
        # - enable both floppy drives, as double sided
        # - set printer output file
        # - disable serial in and set serial output file
        # - disable MIDI in, use MIDI out as fifo file to signify test completion
        dummy = open(self.dummycfg, "w")
        dummy.write("""
[Log]
nAlertDlgLogLevel = 0
bConfirmQuit = FALSE
bNatFeats = FALSE

[System]
bFastBoot = FALSE
bPatchTimerD = FALSE
bFastForward = TRUE
bCompatibleCpu = TRUE
bCycleExactCpu = TRUE
bAddressSpace24 = TRUE
bBlitter = FALSE
bMMU = FALSE
nDSPType = 1
n_FPUType = 0

[ROM]
bPatchTos = TRUE
szCartridgeImageFileName =

[Screen]
nMaxWidth = 832
nMaxHeight = 576
bAllowOverscan = FALSE
bMouseWarp = FALSE
bCrop = TRUE

[HardDisk]
nGemdosDrive = 0
bUseHardDiskDirectory = FALSE
bGemdosHostTime = FALSE
nWriteProtection = 0

[Floppy]
FastFloppy = FALSE
nWriteProtection = 0
EnableDriveA = TRUE
DriveA_NumberOfHeads = 2
EnableDriveB = TRUE
DriveB_NumberOfHeads = 2
szDiskAFileName = blank-a.st.gz

[Printer]
bEnablePrinting = TRUE
szPrintToFileName = %s

[RS232]
bEnableRS232 = TRUE
szInFileName =
szOutFileName = %s
EnableSccB = TRUE
SccBInFileName =
SccBOutFileName = %s

[Midi]
bEnableMidi = TRUE
sMidiInFileName =
sMidiOutFileName = %s
""" % (self.printout, self.serialout, self.serialout, self.fifofile))
        dummy.close()

    def cleanup_all_files(self):
        "clean out any files left over from last run"
        for path in (self.fifofile, "grab0001.png", "grab0001.bmp"):
            if os.path.exists(path):
                os.remove(path)
        self.cleanup_test_files()

    def create_files(self):
        "create files needed during testing"
        if not os.path.exists(self.output):
            os.mkdir(self.output)
        if not os.path.exists(self.fifofile):
            os.mkfifo(self.fifofile)

    def get_screenshot(self, instance, identity):
        "save screenshot of test end result"
        instance.run("screenshot")
        if os.path.isfile("grab0001.png"):
            os.rename("grab0001.png", self.output + identity + ".png")
        elif os.path.isfile("grab0001.bmp"):
            os.rename("grab0001.bmp", self.output + identity + ".bmp")
        else:
            warning("failed to locate screenshot grab0001.{png,bmp}")

    def cleanup_test_files(self):
        "remove unnecessary files at end of test"
        for path in (self.serialout, self.printout):
            if os.path.exists(path):
                os.remove(path)

    def verify_output(self, identity):
        "do verification on all test output"
        ok = True
        # check file truncate
        if "gemdos" in identity:
            # file contents can be checked directly only with GEMDOS HD
            error = verify_file_empty(self.textoutput, identity)
            if error:
                print("ERROR: file wasn't truncated:\n\t%s" % error)
                ok = False
        # check serial output
        error = verify_file_match(self.textinput, self.serialout, identity)
        if error:
            print("ERROR: serial output doesn't match input:\n\t%s" % error)
            ok = False
        # check printer output
        error = verify_file_match(self.textinput, self.printout, identity)
        if error:
            print("ERROR: unexpected printer output:\n\t%s" % error)
            ok = False
        self.cleanup_test_files()
        return ok


    def wait_fifo(self, fifo, timeout):
        "wait_fifo(fifo) -> wait until fifo has input until given timeout"
        print("Waiting %ss for FIFO '%s' input..." % (timeout, self.fifofile))
        sets = select.select([fifo], [], [], timeout)
        if sets[0]:
            print("...test program is READY, read its FIFO for test-case results:")
            try:
                # read can block, make sure it's eventually interrupted
                signal.alarm(timeout)
                line = fifo.readline().strip()
                signal.alarm(0)
                print("=> %s" % line)
                return (True, (line == "success"))
            except IOError:
                pass
        print("ERROR: TIMEOUT without FIFO input, BOOT FAILED")
        return (False, False)


    def open_fifo(self):
        "open FIFO for test program output"
        try:
            signal.alarm(FIFO_WAIT)
            # open returns after Hatari has opened the other
            # end of fifo, or when SIGALARM interrupts it
            fifo = open(self.fifofile, "r")
            # cancel signal
            signal.alarm(0)
            return fifo
        except IOError:
            print("ERROR: FIFO file open('%s') failed" % self.fifofile)
            print("(Hatari PortMidi support not disabled?)")
            return None

    def test(self, identity, testargs, memwait, testwait):
        "run single boot test with given args and waits"
        # Hatari command line options, don't exit if Hatari exits
        instance = hconsole.Main(self.defaults + testargs, False)
        fifo = self.open_fifo()
        if not fifo:
            print("ERROR: failed to get FIFO to Hatari!")
            self.get_screenshot(instance, identity)
            instance.run("kill")
            return (False, False, False, False)
        init_ok = True

        if memwait:
            print("Final start/memcheck wait: %ds" % memwait)
            # pass memory test
            time.sleep(memwait)
            instance.run("keypress %s" % hconsole.Scancode.Space)

        # wait until test program has been run and outputs something to fifo
        prog_ok, tests_ok = self.wait_fifo(fifo, testwait)
        if tests_ok:
            output_ok = self.verify_output(identity)
        else:
            output_ok = False

        # get screenshot after a small wait (to guarantee all
        # test program output got to screen even with frameskip)
        time.sleep(0.2)
        self.get_screenshot(instance, identity)
        # get rid of this Hatari instance
        instance.run("kill")
        return (init_ok, prog_ok, tests_ok, output_ok)


    def prepare_test(self, config, tos, machine, monitor, disk, memory, ttram, bool_opt):
        "compose test ID and Hatari command line args, then call .test()"
        identity = "%s-%s-%s-%s-%dM-%dM" % (tos.name, machine, monitor, disk, memory, ttram)
        testargs = ["--tos", tos.path, "--machine", machine, "--memsize", str(memory)]
        if ttram:
            testargs += ["--addr24", "off", "--ttram", str(ttram)]
        else:
            testargs += ["--addr24", "on"]

        if monitor.startswith("vdi"):
            planes = monitor[-1]
            testargs += ["--vdi-planes", planes]
            if planes == "1":
                testargs += ["--vdi-width", "800", "--vdi-height", "600"]
            elif planes == "2":
                testargs += ["--vdi-width", "640", "--vdi-height", "480"]
            else:
                testargs += ["--vdi-width", "640", "--vdi-height", "400"]
        else:
            testargs += ["--monitor", monitor]

        memwait = tos.memwait
        testwait = tos.fullwait
        mmu = False
        if bool_opt:
            if bool_opt[0] == '--mmu' and bool_opt[1] == 'on':
                mmu = True
            identity += "-%s%s" % (bool_opt[0].replace('-', ''), bool_opt[1])
            testargs += bool_opt
        if config.opts:
            if "--mmu" in config.opts:
                mmu = True
            # pass-through Hatari options
            testargs += config.opts
        if mmu and machine in ("tt", "falcon"):
            # MMU doubles memory wait
            testwait += memwait
            memwait *= 2

        if config.fast:
            testargs += ["--fast-forward", "yes", "--fast-boot", "yes",
                         "--fastfdc", "yes", "--timer-d", "yes"]
        elif machine == "falcon" and disk != "ide":
            # Falcon IDE interface scanning when there's no IDE takes long
            testwait += 8
            memwait += 8

        if disk == "gemdos":
            exit_if_missing([self.testprg, self.textinput])
            # use Hatari autostart, must be last thing added to testargs!
            testargs += [self.testprg]
        # HD supporting TOSes support also INF file autostart, so
        # with them test program can be run with the supplied INF
        # file.
        #
        # However, in case of VDI resolutions the VDI resolution
        # setting requires overriding the INF file...
        #
        # -> always specify directly which program to autostart
        #    with --auto
        elif disk == "floppy":
            if tos.supports_gemdos_hd():
                exit_if_missing([self.bootdesk])
                testargs += ["--disk-a", self.bootdesk, "--auto", self.floppyprg]
            else:
                exit_if_missing([self.bootauto])
                testargs += ["--disk-a", self.bootauto]
            # floppies are slower
            testwait += 3
        elif disk == "acsi":
            exit_if_missing([self.hdimage])
            testargs += ["--acsi", "0=%s" % self.hdimage, "--auto", self.hdprg]
        elif disk == "scsi":
            exit_if_missing([self.hdimage])
            testargs += ["--scsi", "0=%s" % self.hdimage, "--auto", self.hdprg]
        elif disk == "ide":
            exit_if_missing([self.hdimage])
            testargs += ["--ide-master", self.hdimage, "--auto", self.hdprg]
        else:
            raise AssertionError("unknown disk type '%s'" % disk)

        results = self.test(identity, testargs, memwait, testwait)
        self.results[tos.name].append((identity, results))

    def run(self, config):
        "run all TOS boot test combinations"
        config.validate_bools()

        self.results = {}
        for tos in config.images:
            self.results[tos.name] = []
            print("\n***** TESTING: %s *****\n" % tos.name)
            count = 0
            for machine in config.machines:
                if machine not in tos.machines:
                    continue
                for monitor in config.graphics:
                    if not config.valid_monitortype(machine, tos, monitor):
                        continue
                    for memory in config.memsizes:
                        if not config.valid_memsize(machine, memory):
                            continue
                        for disk in config.disks:
                            if not config.valid_disktype(machine, tos, disk):
                                continue
                            for ttram in config.ttrams:
                                if not config.valid_ttram(machine, tos, ttram, disk):
                                    continue
                                no_bools = True
                                for opt in config.bools:
                                    if not config.valid_bool(machine, opt):
                                        continue
                                    no_bools = False
                                    for val in ('on', 'off'):
                                        self.prepare_test(config, tos, machine, monitor, disk, memory, ttram, [opt, val])
                                        count += 1
                                if no_bools:
                                    self.prepare_test(config, tos, machine, monitor, disk, memory, ttram, None)
                                    count += 1
            if not count:
                warning("no matching configuration for TOS '%s'" % tos.name)
        self.cleanup_all_files()

    def summary(self):
        "summarize test results"
        cases = [0, 0, 0, 0]
        passed = [0, 0, 0, 0]
        tosnames = list(self.results.keys())
        tosnames.sort()

        report = open(self.report, "w")
        report.write("\nTest report:\n------------\n")
        for tos in tosnames:
            configs = self.results[tos]
            if not configs:
                report.write("\n+ WARNING: no configurations for '%s' TOS!\n" % tos)
                continue
            report.write("\n+ %s:\n" % tos)
            for config, results in configs:
                # convert True/False bools to FAIL/pass strings
                values = [("FAIL", "pass")[int(r)] for r in results]
                report.write("  - %s: %s\n" % (config, values))
                # update statistics
                for idx in range(len(results)):
                    cases[idx] += 1
                    passed[idx] += results[idx]

        report.write("\nSummary of FAIL/pass values:\n")
        idx = 0
        for line in ("Hatari init", "Test program running", "Test program test-cases", "Test program output"):
            passes, total = passed[idx], cases[idx]
            if passes < total:
                if not passes:
                    result = "all %d FAILED" % total
                else:
                    result = "%d/%d passed" % (passes, total)
            else:
                result = "all %d passed" % total
            report.write("- %s: %s\n" % (line, result))
            idx += 1
        report.write("\n")

        # print report out too
        print("\n--- %s ---" % self.report)
        report = open(self.report, "r")
        for line in report.readlines():
            print(line.strip())


# -----------------------------------------------
def main():
    "tester main function"
    info = "Hatari TOS bootup tester"
    print("\n%s\n%s\n" % (info, "-"*len(info)))
    # avoid global config file
    os.environ["HATARI_TEST"] = "boot"
    config = Config(sys.argv)
    tester = Tester()
    tester.run(config)
    tester.summary()

if __name__ == "__main__":
    main()
