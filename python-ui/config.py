#!/usr/bin/env python
#
# Class and helper functions for handling (Hatari) INI style
# configuration files: loading, saving, setting/getting variables,
# mapping them to sections, listing changes
#
# Copyright (C) 2008-2011 by Eero Tamminen <eerot at berlios>
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

# ------------------------------------------------------
# Helper functions for type safe Hatari configuration variable access.
# Map booleans, integers and strings to Python types, and back to strings.

def value_to_text(key, value):
    "value_to_text(key, value) -> text, convert Python type to string"
    valtype = type(value)
    if valtype == bool:
        assert(key[0] == "b") # bool prefix
        if value:
            text = "TRUE"
        else:
            text = "FALSE"
    elif valtype == int:
        assert(key[0] in ("n", "k")) # numeric/keycode prefix
        text = str(value)
    else:
        assert(key[0] == "s") # string prefix
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
        except ValueError:
            # string
            value = text
    return value


# ------------------------------------------------------
# Handle INI style configuration files as used by Hatari

class ConfigStore:
    def __init__(self, userconfdir, defaults = {}, miss_is_error = True):
        "ConfigStore(userconfdir, fgfile[,defaults,miss_is_error])"
        self.defaults = defaults
        self.userpath = self._get_full_userpath(userconfdir)
        self.miss_is_error = miss_is_error
    
    def _get_full_userpath(self, leafdir):
        "get_userpath(leafdir) -> config file default save path from HOME, CWD or their subdir"
        # user's hatari.cfg can be in home or current work dir,
        # current dir is used only if $HOME fails
        for path in (os.getenv("HOME"), os.getenv("HOMEPATH"), os.getcwd()):
            if path and os.path.exists(path) and os.path.isdir(path):
                if leafdir:
                    hpath = "%s%c%s" % (path, os.path.sep, leafdir)
                    if os.path.exists(hpath) and os.path.isdir(hpath):
                        return hpath
                return path
        return None

    def get_filepath(self, filename):
        "get_filepath(filename) -> return correct full path to config file"
        # user config has preference over system one
        for path in (self.userpath, os.getenv("HATARI_SYSTEM_CONFDIR")):
            if path:
                file = "%s%c%s" % (path, os.path.sep, filename)
                if os.path.isfile(file):
                    return file
        # writing needs path name although it's missing for reading
        return "%s%c%s" % (self.userpath, os.path.sep, filename)
    
    def load(self, path):
        "load(path) -> load given configuration file"
        if os.path.isfile(path):
            sections = self._read(path)
            if sections:
                self.sections = sections
            else:
                print("ERROR: configuration file loading failed!")
                return
        else:
            print("WARNING: configuration file missing!")
            if self.defaults:
                print("-> using dummy 'defaults'.")
            self.sections = self.defaults
        self.path = path
        self.cfgfile = os.path.basename(path)
        self.original = self.get_checkpoint()
        self.changed = False

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
            if line.find('=') < 0:
                print("WARNING: line without key=value pair:\n%s" % line)
                continue
            key, text = [string.strip() for string in line.split('=')]
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
