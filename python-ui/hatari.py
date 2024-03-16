#
# Classes for Hatari emulator instance and mapping its congfiguration
# variables with its command line option.
#
# Copyright (C) 2008-2024 by Eero Tamminen
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
import select
from config import ConfigStore


def _path_quote(path):
    "quote spaces in paths as expected by Hatari socket API"
    return path.replace(" ", "\\ ")


# Running Hatari instance
class Hatari:
    "running hatari instance and methods for communicating with it"
    basepath = "/tmp/hatari-ui-" + str(os.getpid())
    logpath = basepath + ".log"
    tracepath = basepath + ".trace"
    debugpath = basepath + ".debug"
    controlpath = basepath + ".socket"
    server = None # singleton due to path being currently one per user

    def __init__(self, hataribin = None):
        # collect hatari process zombies without waitpid()
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
        if hataribin:
            self.hataribin = hataribin
        else:
            self.hataribin = "hatari"
        self._create_server()
        self.control = None
        self.paused = False
        self.pid = 0

    def is_compatible(self):
        "check Hatari compatibility and return error string if it's not"
        lines = control = mmu = False
        pipe = os.popen(self.hataribin + " -h")
        for line in pipe.readlines():
            if line.find("--control-socket") >= 0:
                control = True
            elif line.find("--mmu") >= 0:
                mmu = True
            lines = True
        try:
            pipe.close()
        except IOError:
            pass
        if not lines:
            return "'%s' not found!" % self.hataribin
        if not control:
            return "Hatari missing required --control-socket option!"
        if not mmu:
            return "Hatari is not the expected (WinUAE CPU core) version!"
        return None

    def save_config(self):
        "ask Hatari to save config.  Return None on success, otherwise Hatari return code"
        pipe = os.popen(self.hataribin + " --saveconfig")
        return pipe.close()

    def _create_server(self):
        if self.server:
            return
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        if os.path.exists(self.controlpath):
            os.unlink(self.controlpath)
        self.server.bind(self.controlpath)
        self.server.listen(1)

    def _send_message(self, msg):
        if self.control:
            self.control.send(bytes(msg, "ASCII"))
            return True
        else:
            print("ERROR: no Hatari (control socket)")
            return False

    def change_option(self, option):
        "change_option(option), changes given Hatari cli option"
        return self._send_message("hatari-option %s\n" % option)

    def set_path(self, key, path):
        "set_path(key, path), sets path with given key"
        return self._send_message("hatari-path %s %s\n" % (key, path))

    def set_device(self, device, enabled):
        # needed because CLI options cannot disable devices, only enable
        "set_path(device, enabled), sets whether given device is enabled or not"
        if enabled:
            return self._send_message("hatari-enable %s\n" % device)
        else:
            return self._send_message("hatari-disable %s\n" % device)

    def trigger_shortcut(self, shortcut):
        "trigger_shortcut(shortcut), triggers given Hatari (keyboard) shortcut"
        return self._send_message("hatari-shortcut %s\n" % shortcut)

    def insert_event(self, event):
        "insert_event(event), synthetizes given key/mouse Atari event"
        return self._send_message("hatari-event %s\n" % event)

    def debug_command(self, cmd):
        "debug_command(command), runs given Hatari debugger command"
        return self._send_message("hatari-debug %s\n" % cmd)

    def pause(self):
        "pause(), pauses Hatari emulation"
        return self._send_message("hatari-stop\n")

    def unpause(self):
        "unpause(), continues Hatari emulation"
        return self._send_message("hatari-cont\n")

    def _open_output_file(self, hataricommand, option, path):
        if os.path.exists(path):
            os.unlink(path)
        # TODO: why fifo doesn't work properly (blocks forever on read or
        #       reads only byte at the time and stops after first newline)?
        #os.mkfifo(path)
        #raw_input("attach strace now, then press Enter\n")

        # ask Hatari to open/create the requested output file...
        hataricommand("%s %s" % (option, path))
        wait = 0.025
        # ...and wait for it to appear before returning it
        for i in range(0, 8):
            time.sleep(wait)
            if os.path.exists(path):
                return open(path, "r")
            wait += wait
        return None

    def open_debug_output(self):
        "open_debug_output() -> file, opens Hatari debugger output file"
        return self._open_output_file(self.debug_command, "f", self.debugpath)

    def open_trace_output(self):
        "open_trace_output() -> file, opens Hatari tracing output file"
        return self._open_output_file(self.change_option, "--trace-file", self.tracepath)

    def open_log_output(self):
        "open_trace_output() -> file, opens Hatari debug log file"
        return self._open_output_file(self.change_option, "--log-file", self.logpath)

    def get_lines(self, fileobj):
        "get_lines(file) -> list of lines readable from given Hatari output file"
        # wait until data is available, then wait for some more
        # and only then the data can be read, otherwise its old
        print("Request&wait data from Hatari...")
        select.select([fileobj], [], [])
        time.sleep(0.1)
        print("...read the data lines")
        lines = fileobj.readlines()
        print("".join(lines))
        return lines

    def enable_embed_info(self):
        "enable_embed_info(), request embedded Hatari window ID change information"
        self._send_message("hatari-embed-info\n")

    def get_embed_info(self):
        "get_embed_info() -> (width, height), get embedded Hatari window size"
        width, height = self.control.recv(12).split(b"x")
        return (int(width), int(height))

    def get_control_socket(self):
        "get_control_socket() -> socket which can be checked for embed ID changes"
        return self.control

    def is_running(self):
        "is_running() -> bool, True if Hatari is running, False otherwise"
        if not self.pid:
            return False
        try:
            os.waitpid(self.pid, os.WNOHANG)
        except OSError as value:
            print("Hatari PID %d had exited in the meanwhile:\n\t%s" % (self.pid, value))
            self.pid = 0
            if self.control:
                self.control.close()
                self.control = None
            return False
        return True

    def run(self, extra_args = None, parent_id = None):
        "run([embedding args][,parent window ID]), runs Hatari"
        # if parent_win given, embed Hatari to it
        pid = os.fork()
        if pid < 0:
            print("ERROR: fork()ing Hatari failed!")
            return
        if pid:
            # in parent
            self.pid = pid
            if self.server:
                print("WAIT hatari to connect to control socket...")
                (self.control, addr) = self.server.accept()
                print("connected!")
        else:
            # child runs Hatari
            env = os.environ
            if parent_id:
                self._set_embed_env(env, parent_id)
            # callers need to take care of confirming quitting
            args = [self.hataribin, "--confirm-quit", "off"]
            if self.server:
                args += ["--control-socket", self.controlpath]
            if extra_args:
                args += extra_args
            print("RUN:", args)
            os.execvpe(self.hataribin, args, env)

    def _set_embed_env(self, env, win_id):
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

    def kill(self):
        "kill(), kill Hatari if it's running"
        if self.is_running():
            os.kill(self.pid, signal.SIGKILL)
            print("killed hatari with PID %d" % self.pid)
            self.pid = 0
        if self.control:
            self.control.close()
            self.control = None


# Mapping of requested values both to Hatari configuration
# and command line options.
#
# By default this doesn't allow setting any other configuration
# variables than the ones that were read from the configuration
# file i.e. you get an exception if configuration variables
# don't match to current Hatari.  So before using this the current
# Hatari configuration should have been saved at least once.
#
# Because of some inconsistencies in the values (see e.g. sound),
# this cannot just do these according to some mapping table, but
# it needs actual method for (each) setting.
class HatariConfigMapping(ConfigStore):
    _paths = {
        "memauto":    ("[Memory]", "szAutoSaveFileName", "Memory snapshot, automatic"),
        "memsave":    ("[Memory]", "szMemoryCaptureFileName", "Memory snapshot, manual"),
        "midiin":     ("[Midi]", "sMidiInFileName", "Midi input"),
        "midiout":    ("[Midi]", "sMidiOutFileName", "Midi output"),
        "rs232in":    ("[RS232]", "szInFileName", "RS232: MFP IO input"),
        "rs232out":   ("[RS232]", "szOutFileName", "RS232: MFP IO output"),
        "sccain":     ("[RS232]", "SccAInFileName", "RS232: SCC-A IO input"),
        "sccaout":    ("[RS232]", "SccAOutFileName", "RS232: SCC-A IO output"),
        "sccalanin":  ("[RS232]", "SccAInFileName", "RS232: SCC-A Lan IO input"),
        "sccalanout": ("[RS232]", "SccAOutFileName", "RS232: SCC-A Lan IO output"),
        "sccbin":     ("[RS232]", "SccBInFileName", "RS232: SCC-B IO input"),
        "sccbout":    ("[RS232]", "SccBOutFileName", "RS232: SCC-B IO output"),
        "printout":   ("[Printer]", "szPrintToFileName", "Printer output"),
        "soundout":   ("[Sound]", "szYMCaptureFileName", "Sound output")
    }
    # enable Hatari v2.5+ options
    has_opts_2_5 = True

    "access methods to Hatari configuration file variables and command line options"
    def __init__(self, hatari):
        confdirs = [".config/hatari", ".hatari"]
        ConfigStore.__init__(self, confdirs)
        conffilename = "hatari.cfg"
        confpath = self.get_filepath(conffilename)
        print("confpath: %s" % confpath)
        error = self.load(confpath)
        if error:
            print("WARNING: %s %s!" % (confpath, error))
        else:
            print("loaded config: %s" % confpath)
        self._hatari = hatari
        self._lock_updates = False
        self._desktop_w = 0
        self._desktop_h = 0
        self._options = []

    def init_compat(self):
        "do config mapping initializations needing config loading to have succeeded"
        # initialize has_opts_<version> attribs for things that may not
        # be anymore valid on Hatari config file and/or command line
        try:
            # added for Hatari v2.5
            self.get("[RS232]", "EnableSccA")
            return
        except KeyError:
            pass
        self.has_opts_2_5 = False
        # drop v2.5 keys and use v2.4 option names
        print("Hatari v2.5 option(s) missing, reverting to v2.4 ones")
        for key in ("sccain","sccaout","sccalanin","sccalanout","sccbin"):
            del(self._paths[key])
        self._paths["sccbout"] = ("[RS232]", "sSccBOutFileName", "RS232: SCC-B IO output")

    def validate(self):
        "exception is thrown if the loaded configuration isn't compatible"
        for method in dir(self):
            if '_' not in method:
                continue
            # check class getters
            starts = method[:method.find("_")]
            if starts != "get":
                continue
            # but ignore getters for other things than config
            ends = method[method.rfind("_")+1:]
            if ends in ("types", "names", "values", "changes", "checkpoint", "filepath"):
                continue
            if ends in ("floppy", "joystick"):
                # use port '0' for checks
                getattr(self, method)(0)
            else:
                getattr(self, method)()

    def _change_option(self, option, filename = ""):
        "handle option changing, and handle filenames appropriately"
        if filename is None:
            option = "%s none" % option
        elif filename:
            if os.path.isfile(filename):
                option = "%s %s" % (option, _path_quote(filename))
            else:
                print("WARN: skipping '%s' option with non-existing filename '%s'" % (option, filename))
                return
        if self._lock_updates:
            self._options.append(option)
        else:
            self._hatari.change_option(option)

    def lock_updates(self):
        "lock_updates(), collect Hatari configuration changes to trigger only single reboot"
        self._lock_updates = True

    def flush_updates(self):
        "flush_updates(), apply collected Hatari configuration changes"
        self._lock_updates = False
        if not self._options:
            return
        self._hatari.change_option(" ".join(self._options))
        self._options = []

    # ------------ paths ---------------
    def get_paths(self):
        paths = []
        for key, item in list(self._paths.items()):
            paths.append((key, self.get(item[0], item[1]), item[2]))
        return paths

    def set_paths(self, paths):
        for key, path in paths:
            self.set(self._paths[key][0], self._paths[key][1], path)
            self._hatari.set_path(key, path)

    # ------------ midi ---------------
    def get_midi(self):
        return self.get("[Midi]", "bEnableMidi")

    def set_midi(self, value):
        self.set("[Midi]", "bEnableMidi", value)
        self._hatari.set_device("midi", value)

    # ------------ printer ---------------
    def get_printer(self):
        return self.get("[Printer]", "bEnablePrinting")

    def set_printer(self, value):
        self.set("[Printer]", "bEnablePrinting", value)
        self._hatari.set_device("printer", value)

    # ------------ RS232 ---------------
    def get_rs232(self):
        return self.get("[RS232]", "bEnableRS232")

    def set_rs232(self, value):
        self.set("[RS232]", "bEnableRS232", value)
        self._hatari.set_device("rs232", value)

    def get_scca(self):
        if not self.has_opts_2_5:
            return False
        return self.get("[RS232]", "EnableSccA")

    def set_scca(self, value):
        if not self.has_opts_2_5:
            return
        self.set("[RS232]", "EnableSccA", value)
        self._hatari.set_device("scca", value)

    def get_scca_lan(self):
        if not self.has_opts_2_5:
            return False
        return self.get("[RS232]", "EnableSccALan")

    def set_scca_lan(self, value):
        if not self.has_opts_2_5:
            return
        self.set("[RS232]", "EnableSccALan", value)
        self._hatari.set_device("sccalan", value)

    def get_sccb(self):
        if not self.has_opts_2_5:
            return self.get("[RS232]", "bEnableSccB")
        return self.get("[RS232]", "EnableSccB")

    def set_sccb(self, value):
        if not self.has_opts_2_5:
            self.set("[RS232]", "bEnableSccB", value)
        else:
            self.set("[RS232]", "EnableSccB", value)
        self._hatari.set_device("sccb", value)

    # ------------ machine ---------------
    def get_machine_types(self):
        return ("ST", "MegaST", "STE", "MegaSTE", "TT", "Falcon")

    def get_machine(self):
        return self.get("[System]", "nModelType")

    def has_accurate_winsize(self):
        return (self.get_machine() < 4)

    def set_machine(self, value):
        self.set("[System]", "nModelType", value)
        self._change_option("--machine %s" % ("st", "megast", "ste", "megaste", "tt", "falcon")[value])

    # ------------ CPU level ---------------
    def get_cpulevel_types(self):
        return ("68000", "68010", "68020", "68030", "68040", "68060")

    def get_cpulevel(self):
        return self.get("[System]", "nCpuLevel")

    def set_cpulevel(self, value):
        if value == 5: # 060
            value = 6
        self.set("[System]", "nCpuLevel", value)
        self._change_option("--cpulevel %d" % value)

    # ------------ compatible ---------------
    def get_compatible(self):
        return self.get("[System]", "bCompatibleCpu")

    def set_compatible(self, value):
        self.set("[System]", "bCompatibleCpu", value)
        self._change_option("--compatible %s" % value)

    # ------------ CPU exact ---------------
    def get_cycle_exact(self):
        return self.get("[System]", "bCycleExactCpu")

    def set_cycle_exact(self, value):
        self.set("[System]", "bCycleExactCpu", value)
        self._change_option("--cpu-exact %s" % value)

    # ------------ MMU ---------------
    def get_mmu(self):
        return self.get("[System]", "bMMU")

    def set_mmu(self, value):
        self.set("[System]", "bMMU", value)
        self._change_option("--mmu %s" % value)

    # ------------ CPU clock ---------------
    def get_cpuclock_types(self):
        return ("8 MHz", "16 MHz", "32 MHz")

    def get_cpuclock(self):
        clocks = {8:0, 16: 1, 32:2}
        return clocks[self.get("[System]", "nCpuFreq")]

    def set_cpuclock(self, value):
        clocks = [8, 16, 32]
        if value < 0 or value > 2:
            print("WARNING: CPU clock idx %d, clock fixed to 8 Mhz" % value)
            value = 8
        else:
            value = clocks[value]
        self.set("[System]", "nCpuFreq", value)
        self._change_option("--cpuclock %d" % value)

    # ------------ FPU type ---------------
    def get_fpu_types(self):
        return ("None", "68881", "68882", "Internal")

    def get_fpu_type(self):
        return self.get("[System]", "n_FPUType")

    def set_fpu_type(self, value):
        self.set("[System]", "n_FPUType", value)
        self._change_option("--fpu %s" % self.get_fpu_types()[value])

    # ------------ SW FPU --------------
    def get_fpu_soft(self):
        return self.get("[System]", "bSoftFloatFPU")

    def set_fpu_soft(self, value):
        self.set("[System]", "bSoftFloatFPU", value)
        self._change_option("--fpu-softfloat %s" % value)

    # ------------ ST blitter --------------
    def get_blitter(self):
        return self.get("[System]", "bBlitter")

    def set_blitter(self, value):
        self.set("[System]", "bBlitter", value)
        self._change_option("--blitter %s" % value)

    # ------------ DSP type ---------------
    def get_dsp_types(self):
        return ("None", "Dummy", "Emulated")

    def get_dsp(self):
        return self.get("[System]", "nDSPType")

    def set_dsp(self, value):
        self.set("[System]", "nDSPType", value)
        self._change_option("--dsp %s" % ("none", "dummy", "emu")[value])

    # ------------ Timer-D ---------------
    def get_timerd(self):
        return self.get("[System]", "bPatchTimerD")

    def set_timerd(self, value):
        self.set("[System]", "bPatchTimerD", value)
        self._change_option("--timer-d %s" % value)

    # ------------ fastforward ---------------
    def get_fastforward(self):
        return self.get("[System]", "bFastForward")

    def set_fastforward(self, value):
        self.set("[System]", "bFastForward", value)
        self._change_option("--fast-forward %s" % value)

    # ------------ sound ---------------
    def get_sound_values(self):
        # 48kHz, 44.1kHz and STE/TT/Falcon DMA 50066Hz divisible values
        return ("6000", "6258", "8000", "11025", "12000", "12517",
                "16000", "22050", "24000", "25033", "32000",
                "44100", "48000", "50066")

    def get_sound(self):
        enabled = self.get("[Sound]", "bEnableSound")
        hz = str(self.get("[Sound]", "nPlaybackFreq"))
        idx = self.get_sound_values().index(hz)
        return (enabled, idx)

    def set_sound(self, enabled, idx):
        # map get_sound_values() index to Hatari config
        hz = self.get_sound_values()[idx]
        self.set("[Sound]", "nPlaybackFreq", int(hz))
        self.set("[Sound]", "bEnableSound", enabled)
        # and to cli option
        if enabled:
            self._change_option("--sound %s" % hz)
        else:
            self._change_option("--sound off")

    def get_ymmixer_types(self):
        return ("Linear", "ST table", "Math model")

    def get_ymmixer(self):
        # values for types are start from 1, not 0
        return self.get("[Sound]", "YmVolumeMixing")-1

    def set_ymmixer(self, value):
        ymmixer_types = ("linear", "table", "model")
        self.set("[Sound]", "YmVolumeMixing", value+1)
        self._change_option("--ym-mixing %s" % ymmixer_types[value])

    def get_bufsize(self):
        return self.get("[Sound]", "nSdlAudioBufferSize")

    def set_bufsize(self, value):
        value = int(value)
        if value < 10 or value > 100:
            value = 0
        self.set("[Sound]", "nSdlAudioBufferSize", value)
        self._change_option("--sound-buffer-size %d" % value)

    def get_sync(self):
        return self.get("[Sound]", "bEnableSoundSync")

    def set_sync(self, value):
        self.set("[Sound]", "bEnableSoundSync", value)
        self._change_option("--sound-sync %s" % value)

    def get_mic(self):
        return self.get("[Sound]", "bEnableMicrophone")

    def set_mic(self, value):
        self.set("[Sound]", "bEnableMicrophone", value)
        self._change_option("--mic %s" % value)

    # ----------- joystick --------------
    def get_joystick_types(self):
        return ("Disabled", "Real joystick", "Keyboard")

    def get_joystick_names(self):
        return (
        "ST Joystick 0",
        "ST Joystick 1",
        "STE Joypad A",
        "STE Joypad B",
        "Parport stick 1",
        "Parport stick 2"
        )

    def get_joystick(self, port):
        # return index to get_joystick_values() array
        return self.get("[Joystick%d]" % port, "nJoystickMode")

    def set_joystick(self, port, value):
        # map get_sound_values() index to Hatari config
        self.set("[Joystick%d]" % port, "nJoystickMode", value)
        joytype = ("none", "real", "keys")[value]
        self._change_option("--joy%d %s" % (port, joytype))

    # ------------ floppy handling ---------------
    def get_floppydir(self):
        return self.get("[Floppy]", "szDiskImageDirectory")

    def set_floppydir(self, path):
        return self.set("[Floppy]", "szDiskImageDirectory", path)

    def get_floppy(self, drive):
        return self.get("[Floppy]", "szDisk%cFileName" % ("A", "B")[drive])

    def set_floppy(self, drive, filename):
        self.set("[Floppy]", "szDisk%cFileName" %  ("A", "B")[drive], filename)
        self._change_option("--disk-%c" % ("a", "b")[drive], filename)

    def get_floppy_drives(self):
        return (self.get("[Floppy]", "EnableDriveA"), self.get("[Floppy]", "EnableDriveB"))

    def set_floppy_drives(self, drives):
        idx = 0
        for drive in ("A", "B"):
            value = drives[idx]
            self.set("[Floppy]", "EnableDrive%c" % drive, value)
            self._change_option("--drive-%c %s" % (drive.lower(), value))
            idx += 1

    def get_fastfdc(self):
        return self.get("[Floppy]", "FastFloppy")

    def set_fastfdc(self, value):
        self.set("[Floppy]", "FastFloppy", value)
        self._change_option("--fastfdc %s" % value)

    def get_doublesided(self):
        driveA = self.get("[Floppy]", "DriveA_NumberOfHeads")
        driveB = self.get("[Floppy]", "DriveB_NumberOfHeads")
        if driveA > 1 or driveB > 1:
            return True
        return False

    def set_doublesided(self, value):
        if value: sides = 2
        else:     sides = 1
        for drive in ("A", "B"):
            self.set("[Floppy]", "Drive%c_NumberOfHeads" % drive, sides)
            self._change_option("--drive-%c-heads %d" % (drive.lower(), sides))

    # ------------- disk protection -------------
    def get_protection_types(self):
        return ("Off", "On", "Auto")

    def get_floppy_protection(self):
        return self.get("[Floppy]", "nWriteProtection")

    def get_hd_protection(self):
        return self.get("[HardDisk]", "nWriteProtection")

    def set_floppy_protection(self, value):
        self.set("[Floppy]", "nWriteProtection", value)
        self._change_option("--protect-floppy %s" % self.get_protection_types()[value])

    def set_hd_protection(self, value):
        self.set("[HardDisk]", "nWriteProtection", value)
        self._change_option("--protect-hd %s" % self.get_protection_types()[value])

    # ------------ GEMDOS HD (dir) emulation ---------------
    def get_hd_cases(self):
        return ("No conversion", "Upper case", "Lower case")

    def get_hd_case(self):
        return self.get("[HardDisk]", "nGemdosCase")

    def set_hd_case(self, value):
        values = ("off", "upper", "lower")
        self.set("[HardDisk]", "nGemdosCase", value)
        self._change_option("--gemdos-case %s" % values[value])

    def get_hd_drives(self):
        return ['skip ACSI/IDE'] + [("%c:" % x) for x in range(ord('C'), ord('Z')+1)]

    def get_hd_drive(self):
        return self.get("[HardDisk]", "nGemdosDrive") + 1

    def set_hd_drive(self, value):
        value -= 1
        self.set("[HardDisk]", "nGemdosDrive", value)
        drive = chr(ord('C') + value)
        if value < 0:
            drive = "skip"
        self._change_option("--gemdos-drive %s" % drive)

    def get_hd_dir(self):
        self.get("[HardDisk]", "bUseHardDiskDirectory") # for validation
        return self.get("[HardDisk]", "szHardDiskDirectory")

    def set_hd_dir(self, dirname):
        self.set("[HardDisk]", "szHardDiskDirectory", dirname)
        if dirname:
            if os.path.isdir(dirname):
                self.set("[HardDisk]", "bUseHardDiskDirectory", True)
                self._change_option("--harddrive %s" % _path_quote(dirname))
        else:
            self._change_option("--harddrive none")

    # ------------ ACSI HD (file) ---------------
    def get_acsi_image(self):
        self.get("[ACSI]", "bUseDevice0")
        return self.get("[ACSI]", "sDeviceFile0")

    def set_acsi_image(self, filename):
        if filename and os.path.isfile(filename):
            self.set("[ACSI]", "bUseDevice0", True)
        self.set("[ACSI]", "sDeviceFile0", filename)
        self._change_option("--acsi", filename)

    # ------------ IDE master (file) ---------------
    def get_idemaster_image(self):
        return self.get("[IDE]", "sDeviceFile0")

    def set_idemaster_image(self, filename):
        if filename and os.path.isfile(filename):
            self.set("[IDE]", "bUseDevice0", True)
        self.set("[IDE]", "sDeviceFile0", filename)
        self._change_option("--ide-master", filename)

    # ------------ IDE slave (file) ---------------
    def get_ideslave_image(self):
        return self.get("[IDE]", "sDeviceFile1")

    def set_ideslave_image(self, filename):
        if filename and os.path.isfile(filename):
            self.set("[IDE]", "bUseDevice1", True)
        self.set("[IDE]", "sDeviceFile1", filename)
        self._change_option("--ide-slave", filename)

    # ------------ TOS ROM ---------------
    def get_tos(self):
        return self.get("[ROM]", "szTosImageFileName")

    def set_tos(self, filename):
        self.set("[ROM]", "szTosImageFileName", filename)
        self._change_option("--tos", filename)

    # ------------ memory ---------------
    def get_memory_names(self):
        # empty item in list shouldn't be shown, filter them out
        return ("512kB", "1MB", "2MB", "4MB", "8MB", "14MB")

    def get_memory(self):
        "return index to what get_memory_names() returns"
        sizemap = (0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 5)
        memsize = self.get("[Memory]", "nMemorySize")
        if memsize >= 1024 and memsize <= 14*1024:
            memsize //= 1024
        elif memsize >= 512:
            memsize = 0
        elif memsize < 0 or memsize >= len(sizemap):
            memsize = 1
        return sizemap[memsize]

    def set_memory(self, idx):
        # map memory item index to memory size
        sizemap = (0, 1, 2, 4, 8, 14)
        if idx >= 0 and idx < len(sizemap):
            memsize = sizemap[idx]
        else:
            memsize = 1
        if memsize:
            memsize *= 1024
        else:
            memsize = 512
        self.set("[Memory]", "nMemorySize", memsize)
        self._change_option("--memsize %d" % memsize)

    def get_ttram(self):
        return self.get("[Memory]", "nTTRamSize")

    def set_ttram(self, memsize, machine):
        # enforce 4MB granularity used also by Hatari
        memsize = (int(memsize)+3) & ~3
        self.set("[Memory]", "nTTRamSize", memsize)
        self._change_option("--ttram %d" % memsize)
        if memsize and machine in ("TT", "Falcon"):
            # TT-RAM need 32-bit addressing (i.e. disable 24-bit)
            self.set("[System]", "bAddressSpace24", False)
            self._change_option("--addr24 off")
        else:
            # switch 24-bit addressing back for compatibility
            self.set("[System]", "bAddressSpace24", True)
            self._change_option("--addr24 on")

    # ------------ monitor ---------------
    def get_monitor_types(self):
        return ("Mono", "RGB", "VGA", "TV")

    def get_monitor(self):
        return self.get("[Screen]", "nMonitorType")

    def set_monitor(self, value):
        self.set("[Screen]", "nMonitorType", value)
        self._change_option("--monitor %s" % ("mono", "rgb", "vga", "tv")[value])

    # ------------ frameskip ---------------
    def get_frameskip_names(self):
        return (
            "Disabled",
            "1 frame",
            "2 frames",
            "3 frames",
            "4 frames",
            "Automatic"
        )

    def get_frameskip(self):
        fs = self.get("[Screen]", "nFrameSkips")
        if fs < 0 or fs > 5:
            return 5
        return fs

    def set_frameskip(self, value):
        value = int(value) # guarantee correct type
        self.set("[Screen]", "nFrameSkips", value)
        self._change_option("--frameskips %d" % value)

    # ------------ VBL slowdown ---------------
    def get_slowdown_names(self):
        return ("Disabled", "2x", "3x", "4x", "5x", "6x", "8x")

    def set_slowdown(self, value):
        value = 1 + int(value)
        self._change_option("--slowdown %d" % value)

    # ------------ spec512 ---------------
    def get_spec512threshold(self):
        return self.get("[Screen]", "nSpec512Threshold")

    def set_spec512threshold(self, value):
        value = int(value) # guarantee correct type
        self.set("[Screen]", "nSpec512Threshold", value)
        self._change_option("--spec512 %d" % value)

    # --------- keep desktop res -----------
    def get_desktop(self):
        return self.get("[Screen]", "bKeepResolution")

    def set_desktop(self, value):
        self.set("[Screen]", "bKeepResolution", value)
        self._change_option("--desktop %s" % value)

    # ------------ force max ---------------
    def get_force_max(self):
        return self.get("[Screen]", "bForceMax")

    def set_force_max(self, value):
        self.set("[Screen]", "bForceMax", value)
        self._change_option("--force-max %s" % value)

    # ------------ show borders ---------------
    def get_borders(self):
        return self.get("[Screen]", "bAllowOverscan")

    def set_borders(self, value):
        self.set("[Screen]", "bAllowOverscan", value)
        self._change_option("--borders %s" % value)

    # ------------ show statusbar ---------------
    def get_statusbar(self):
        return self.get("[Screen]", "bShowStatusbar")

    def set_statusbar(self, value):
        self.set("[Screen]", "bShowStatusbar", value)
        self._change_option("--statusbar %s" % value)

    # ------------ crop statusbar ---------------
    def get_crop(self):
        return self.get("[Screen]", "bCrop")

    def set_crop(self, value):
        self.set("[Screen]", "bCrop", value)
        self._change_option("--crop %s" % value)

    # ------------ show led ---------------
    def get_led(self):
        return self.get("[Screen]", "bShowDriveLed")

    def set_led(self, value):
        self.set("[Screen]", "bShowDriveLed", value)
        self._change_option("--drive-led %s" % value)

    # ------------ monitor aspect ratio ---------------
    def get_aspectcorrection(self):
        return self.get("[Screen]", "bAspectCorrect")

    def set_aspectcorrection(self, value):
        self.set("[Screen]", "bAspectCorrect", value)
        self._change_option("--aspect %s" % value)

    # ------------ max window size ---------------
    def set_desktop_size(self, w, h):
        self._desktop_w = w
        self._desktop_h = h

    def get_desktop_size(self):
        return (self._desktop_w, self._desktop_h)

    def get_max_size(self):
        w = self.get("[Screen]", "nMaxWidth")
        h = self.get("[Screen]", "nMaxHeight")
        # default to desktop size?
        if not (w or h):
            w = self._desktop_w
            h = self._desktop_h
        return (w, h)

    def set_max_size(self, w, h):
        # guarantee correct type (Gtk float -> config int)
        w = int(w); h = int(h)
        self.set("[Screen]", "nMaxWidth", w)
        self.set("[Screen]", "nMaxHeight", h)
        self._change_option("--max-width %d" % w)
        self._change_option("--max-height %d" % h)

    # TODO: remove once UI doesn't need this anymore
    def set_zoom(self, value):
        print("Just setting Zoom, configuration doesn't anymore have setting for this.")
        if value:
            zoom = 2
        else:
            zoom = 1
        self._change_option("--zoom %d" % zoom)

    # ------------ configured Hatari window size ---------------
    def get_window_size(self):
        if self.get("[Screen]", "bFullScreen"):
            print("WARNING: don't start Hatari UI with fullscreened Hatari!")

        # VDI resolution?
        if self.get("[Screen]", "bUseExtVdiResolutions"):
            width = self.get("[Screen]", "nVdiWidth")
            height = self.get("[Screen]", "nVdiHeight")
            return (width, height)

        # window sizes for other than ST & STE can differ
        if self.has_accurate_winsize():
            videl = False
        else:
            print("WARNING: With Videl, window size is unknown -> may be inaccurate!")
            videl = True

        # mono monitor?
        if self.get_monitor() == 0:
            return (640, 400)

        # no, color
        width = 320
        height = 200
        # statusbar?
        if self.get_statusbar():
            sbar = 12
            height += sbar
        else:
            sbar = 0
        # zoom?
        maxw, maxh = self.get_max_size()
        if 2*width <= maxw and 2*height <= maxh:
            width *= 2
            height *= 2
            zoom = 2
        else:
            zoom = 1
        # overscan borders?
        if self.get_borders() and not videl:
            # properly aligned borders on top of zooming
            leftx = (maxw-width)//zoom
            borderx = 2*(min(48,leftx//2)//16)*16
            lefty = (maxh-height)//zoom
            bordery = min(29+47, lefty)
            width += zoom*borderx
            height += zoom*bordery

        return (width, height)
