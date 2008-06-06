#!/usr/bin/env python
#
# Class and helper functions for handling (Hatari) INI style
# configuration files: loading, saving, setting/getting variables,
# mapping them to sections, listing changes
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

class ConfigStore():
    defaultpath = "%s%c.hatari" % (os.getenv("HOME"), os.path.sep)

    def __init__(self, cfgfile, defaults = {}, miss_is_error = True):
        "ConfigStore(defaults,cfgfile[,miss_is_error])"
        self.changed = False
        self.miss_is_error = miss_is_error
        path = self.get_path(cfgfile)
        if path:
            self.sections = self.load(path)
            if self.sections:
                print "Loaded configuration file:", path
            else:
                print "WARNING: configuration file '%' loading failed" % path
                path = None
        else:
            print "WARNING: configuration file '%s' missing, using defaults" % cfgfile
            self.sections = defaults
        self.original = self.get_checkpoint()
        self.cfgfile = cfgfile
        self.path = path

    def get_path(self, cfgfile):
        "get_path(cfgfile) -> path or None, check first CWD & then HOME for cfgfile"
        # hatari.cfg can be in home or current work dir
        for path in (os.getcwd(), os.getenv("HOME")):
            if path:
                path = self._check_path(path, cfgfile)
                if path:
                    return path
        return None

    def _check_path(self, path, cfgfile):
        """check_path(path,cfgfile) -> path
        
        return full path if cfg in path/.hatari/ or in path prefixed with '.'"""
        sep = os.path.sep
        testpath = "%s%c.hatari%c%s" % (path, sep, sep, cfgfile)
        if os.path.exists(testpath):
            return testpath
        testpath = "%s%c.%s" % (path, sep, cfgfile)
        if os.path.exists(testpath):
            return testpath
        return None
    
    def load(self, path):
        "load(path) -> (all keys, section2key mappings)"
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
                    print "WARNING: section '%s' twice in configuration" % line
                if seckeys:
                    sections[name] = seckeys
                    seckeys = {}
                name = line
                continue
            if line.find('=') < 0:
                print "WARNING: line without key=value pair:\n%s" % line
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
                    changed.append(("%s.%s" % (section, key), value))
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
                raise AttributeError, "no section '%s'" % section
            self.sections[section] = {}
        if key not in self.sections[section]:
            if self.miss_is_error:
                raise AttributeError, "key '%s' not in section '%s'" % (key, section)
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
        sections = self.sections.keys()
        sections.sort()
        for name in sections:
            fileobj.write("%s\n" % name)
            keys = self.sections[name].keys()
            keys.sort()
            for key in keys:
                fileobj.write("%s = %s\n" % (key, self.sections[name][key]))
            fileobj.write("\n")

    def save(self):
        "save(), if configuration changed, save it"
        if not self.changed:
            print "No configuration changes to save, skipping"
            return            
        if not self.path:
            print "WARNING: no existing configuration file, trying to create one"
            if not os.path.exists(self.defaultpath):
                os.mkdir(self.defaultpath)
            self.path = "%s%c%s" % (self.defaultpath, os.path.sep, self.cfgfile)
        #fileobj = sys.stdout
        fileobj = open(self.path, "w")
        if fileobj:
            self.write(fileobj)
            print "Saved configuration file:", self.path
        else:
            print "ERROR: opening '%s' for saving failed" % self.path
