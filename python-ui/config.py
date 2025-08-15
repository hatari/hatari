# Class and helper functions for handling (Hatari) INI style
# configuration files: loading, saving, setting/getting variables,
# mapping them to sections, listing changes
#
# Copyright (C) 2008-2012,2016-2020 by Eero Tamminen
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
# mapping from Hatari config variable name to type id (Bool, Int, String)
from conftypes import conftypes

# ------------------------------------------------------
# Helper functions for type safe Hatari configuration variable access.
# Map booleans, integers and strings to Python types, and back to strings.

def value_to_text(key, value):
    "value_to_text(key, value) -> text, convert Python type to string"
    #if key not in conftypes:
    #    print("ERROR: key '%s' missing!" % key)
    assert(key in conftypes)
    valtype = type(value)
    if valtype == bool:
        assert(conftypes[key] == "Bool")
        if value:
            text = "TRUE"
        else:
            text = "FALSE"
    elif valtype in (float, int):
        assert(conftypes[key] in ("Float", "Int"))
        text = str(value)
    else:
        # keyboard key name or any string
        assert(conftypes[key] in ("Key", "String"))
        if value == None:
            text = ""
        else:
            text = value
    return text

def text_to_value(text):
    "text_to_value(text) -> value, convert INI file values to real types"
    # bool?
    upper = text.upper()
    if upper == "FALSE":
        value = False
    elif upper == "TRUE":
        value = True
    else:
        try:
            # integer?
            value = int(text)
            # if not exact, maybe it's float?
            if text != '%d' % value:
                value = float(text)
        except ValueError:
            # string
            value = text
    return value


# ------------------------------------------------------
# Handle INI style configuration files as used by Hatari

class ConfigStore:
    def __init__(self, confdirs, defaults = {}, miss_is_error = True):
        "ConfigStore(userconfdir, fgfile[,defaults,miss_is_error])"
        self.defaults = defaults
        self.userpath = self._get_full_userpath(confdirs)
        # this is only about ConfigStore checks i.e. value existing in
        # current Hatari config, value_to_text() will still fail if
        # config name isn't even in conftypes (= all Hatari configs)
        self.miss_is_error = miss_is_error
        self.changed = False

    def _get_full_userpath(self, confdirs):
        "get_userpath(leafdir) -> config file default save path from HOME, CWD or their subdir"
        # user's hatari.cfg can be in home or current work dir,
        # current dir is used only if $HOME fails
        for path in (os.getenv("HOME"), os.getenv("HOMEPATH"), os.getcwd()):
            if path and os.path.exists(path) and os.path.isdir(path):
                for leafdir in confdirs:
                    if leafdir:
                        hpath = "%s%c%s" % (path, os.path.sep, leafdir)
                        if os.path.exists(hpath) and os.path.isdir(hpath):
                            return hpath
                return path
        return None

    def get_filepath(self, filename):
        "get_filepath(filename) -> return correct full path to config file"
        # user config has preference over system one
        sep = os.path.sep
        for path in (self.userpath, os.getenv("HATARI_SYSTEM_CONFDIR")):
            if path:
                confpath = "%s%c%s" % (path, sep, filename)
                if os.path.isfile(confpath):
                    return confpath
        # writing needs path name although it's missing for reading
        return "%s%c%s" % (self.userpath, sep, filename)

    def load(self, path):
        "load(path): load given configuration file -> message on error, None on success"
        if os.path.isfile(path):
            sections = self._read(path)
            if sections:
                self.sections = sections
            else:
                return "loading failed"
        else:
            if not self.defaults:
                return "file missing"
            print("-> using dummy 'defaults'.")
            self.sections = self.defaults
        self.path = path
        self.cfgfile = os.path.basename(path)
        self.original = self.get_checkpoint()
        self.changed = False
        return None

    def is_loaded(self):
        "is_loaded() -> True if configuration loading succeeded"
        if self.sections:
            return True
        return False

    def get_path(self):
        "get_path() -> configuration file path"
        return self.path

    def _read(self, path):
        "_read(path) -> (all keys, section2key mappings)"
        print("Reading configuration file '%s'..." % path)
        config = open(path, "r")
        if not config:
            return ({}, {})
        name = "[_orphans_]"
        seckeys = {}
        sections = {}
        for line in config.readlines():
            line = line.strip()
            if not line or line[0] == '#':
                continue
            if line[0] == '[':
                if line in sections:
                    print("WARNING: section '%s' twice in configuration" % line)
                if seckeys:
                    sections[name] = seckeys
                    seckeys = {}
                name = line
                continue
            offset = line.find('=')
            if offset < 0:
                print("WARNING: line without key=value pair:\n%s" % line)
                continue
            key = line[:offset].strip()
            text = line[offset+1:].strip()
            seckeys[key] = text_to_value(text)
        if seckeys:
            sections[name] = seckeys
        return sections

    def get_checkpoint(self):
        "get_checkpoint() -> checkpoint, get the state of variables at this point"
        checkpoint = {}
        for section in self.sections.keys():
            checkpoint[section] = self.sections[section].copy()
        return checkpoint

    def get_checkpoint_changes(self, checkpoint):
        "get_checkpoint_changes() -> list of (key, value) pairs for later changes"
        changed = []
        if not self.changed:
            return changed
        for section in self.sections.keys():
            if section not in checkpoint:
                for key, value in self.sections[section].items():
                    changed.append((key, value))
                continue
            for key, value in self.sections[section].items():
                if (key not in checkpoint[section] or
                value != checkpoint[section][key]):
                    text = value_to_text(key, value)
                    changed.append(("%s.%s" % (section, key), text))
        return changed

    def revert_to_checkpoint(self, checkpoint):
        "revert_to_checkpoint(checkpoint), revert to given checkpoint"
        self.sections = checkpoint

    def get(self, section, key):
        return self.sections[section][key]

    def set(self, section, key, value):
        "set(section,key,value), set given key to given section"
        if section not in self.sections:
            if self.miss_is_error:
                raise AttributeError("no section '%s'" % section)
            self.sections[section] = {}
        if key not in self.sections[section]:
            if self.miss_is_error:
                raise AttributeError("key '%s' not in section '%s'" % (key, section))
            self.sections[section][key] = value
            self.changed = True
        elif self.sections[section][key] != value:
            self.changed = True
        self.sections[section][key] = value

    def is_changed(self):
        "is_changed() -> True if current configuration is changed"
        return self.changed

    def get_changes(self):
        "get_changes(), return (key, value) list for each changed config option"
        return self.get_checkpoint_changes(self.original)

    def write(self, fileobj):
        "write(fileobj), write current configuration to given file object"
        sections = list(self.sections.keys())
        sections.sort()
        for name in sections:
            fileobj.write("%s\n" % name)
            keys = list(self.sections[name].keys())
            keys.sort()
            for key in keys:
                value = value_to_text(key, self.sections[name][key])
                fileobj.write("%s = %s\n" % (key, value))
            fileobj.write("\n")

    def save(self):
        "save() -> path, if configuration changed, save it"
        if not self.changed:
            print("No configuration changes to save, skipping")
            return None
        fileobj = None
        if self.path:
            try:
                fileobj = open(self.path, "w")
            except:
                pass
        if not fileobj:
            print("WARNING: non-existing/writable configuration file, creating a new one...")
            if not os.path.exists(self.userpath):
                os.makedirs(self.userpath)
            self.path = "%s%c%s" % (self.userpath, os.path.sep, self.cfgfile)
            fileobj = open(self.path, "w")
        if not fileobj:
            print("ERROR: opening '%s' for saving failed" % self.path)
            return None
        self.write(fileobj)
        print("Saved configuration file:", self.path)
        self.changed = False
        return self.path

    def save_as(self, path):
        "save_as(path) -> path, save configuration to given file and select it"
        assert(path)
        if not os.path.exists(os.path.dirname(path)):
            os.makedirs(os.path.dirname(path))
        self.path = path
        self.changed = True
        return self.save()

    def save_tmp(self, path):
        "save_tmp(path) -> path, save configuration to given file without selecting it"
        if not os.path.exists(os.path.dirname(path)):
            os.makedirs(os.path.dirname(path))
        fileobj = open(path, "w")
        if not fileobj:
            print("ERROR: opening '%s' for saving failed" % path)
            return None
        self.write(fileobj)
        print("Saved temporary configuration file:", path)
        return path
