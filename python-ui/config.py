#!/usr/bin/env python
#
# Classes for handling (Hatari) INI style configuration files:
# loading, saving, setting/getting variables, mapping them to
# sections, listing changes
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


# Class for nicer configuration variable access.  Maps booleans, integers
# and strings to Python types when the variables dictionary is set, and
# back to strings when is requested.
#
# The actual configuration variables are mapped to instance object attributes
# with a bit of Python magic so other code doesn't need to deal with strings.
# This means that the class's own attribute access is more complicated.
class ConfigVariables():
    
    def __init__(self, variables, miss_is_error):
        "ConfigVariables(vars), set dict of allowed variables + their initial values"
        if miss_is_error and not variables:
            raise ValueError, "empty configuration"
        self.set_all(variables)
        # has to be done like this to avoid infinite recursion with __setattr__
        self.__dict__["__miss_is_error"] = miss_is_error
        self.__dict__["__changed"] = False
    
    def set_all(self, variables):
        "set_all(variables), converts given dict values to Python types"
        items = {}
        for key, text in variables.items():
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
            items[key] = value
        # has to be done like this to avoid infinite recursion with __setattr__
        self.__dict__["__vars"] = items
    
    def get_all(self):
        "get_all(variables), converts Python type dict values to strings"
        items = {}
        for key, value in self.__dict__["__vars"].items():
            #print key, value
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
            items[key] = text
        return items

    def get_checkpoint(self):
        "get_checkpoint() -> checkpoint, get the state of variables at this point"
        return self.__dict__["__vars"].copy()
    
    def get_checkpoint_changes(self, items):
        "get_checkpoint_changes() -> list of (key, value) pairs for later changes"
        changed = []
        if self.is_changed():
            for key, value in self.__dict__["__vars"].items():
                if key not in items or items[key] != value:
                    changed.append((key, value))
        return changed
    
    def revert_to_checkpoint(self, items):
        "revert_to_checkpoint(checkpoint), revert to given checkpoint"
        self.__dict__["__vars"] = items
    
    def is_changed(self):
        "is_changed() -> bool, whether any variables were changed"
        return self.__dict__["__changed"]

    def get(self, key):
        "get(key) -> value"
        return self.__getattr__(key)

    def set(self, key, value):
        "set(key, value)"
        self.__setattr__(key, value)
    
    def __getattr__(self, key):
        print "get key %s" % key
        if key in self.__dict__["__vars"]:
            return self.__dict__["__vars"][key]
        else:
            raise AttributeError, "no config variable '%s' to get" % key

    def __setattr__(self, key, value):
        print "set %s=%s" % (key, str(value))
        if key in self.__dict__["__vars"]:
            if value != self.__dict__["__vars"][key]:
                self.__dict__["__vars"][key] = value
                self.__dict__["__changed"] = True
        elif self.__dict__["__miss_is_error"]:
            raise AttributeError, "no config variable '%s' to set to '%s'" % (key, str(value))


# Handle INI style configuration files as used by Hatari
#
# Hatari configuration variable names are unique within,
# whole configuration file, therefore APIs don't need to
# use sections, just map them them on load&save.
class ConfigStore():
    defaultpath = "%s%c.hatari" % (os.getenv("HOME"), os.path.sep)

    def __init__(self, defaults, cfgfile, miss_is_error = True):
        "ConfigStore(defaults,cfgfile[,miss_is_error])"
        self.changed = False
        path = self.get_path(cfgfile)
        if path:
            variables, sections = self.load(path)
            if variables:
                print "Loaded configuration file:", path
            else:
                print "WARNING: configuration file '%' loading failed" % path
                path = None
        else:
            print "WARNING: configuration file '%s' missing, using defaults" % cfgfile
            sections, variables = defaults
        self.variables = ConfigVariables(variables, miss_is_error)
        self.original = self.variables.get_checkpoint()
        self.sections = sections
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
        sections = {}
        allkeys = {}
        seckeys = []
        for line in config.readlines():
            line = line.strip()
            if not line or line[0] == '#':
                continue
            if line[0] == '[':
                if line in sections:
                    print "WARNING: section '%s' twice in configuration" % line
                if seckeys:
                    sections[name] = seckeys
                    seckeys = []
                name = line
                continue
            if line.find('=') < 0:
                print "WARNING: line without key=value pair:\n%s" % line
                continue
            key, value = [string.strip() for string in line.split('=')]
            allkeys[key] = value
            seckeys.append(key)
        if seckeys:
            sections[name] = seckeys
        return allkeys, sections

    def add(self, section, key, value):
        "add(section,key,value), add given key to given section"
        if not section in self.sections:
            self.sections[section] = []
        elif key in self.sections[section]:
            print "ERROR: key '%s' already in section '%s'" % (key, section)
            return
        self.sections[section][key] = value
        self.variables.set(key, value)
        
    def is_changed(self):
        "is_changed() -> True if current configuration is changed"
        return self.variables.is_changed()

    def get_changes(self):
        "get_changes(), return (key, value) list for each changed config option"
        return self.variables.get_checkpoint_changes(self.original)
    
    def write(self, fileobj):
        "write(fileobj), write current configuration to given file object"
        variables = self.variables.get_all()
        sections = self.sections.keys()
        sections.sort()
        for section in sections:
            fileobj.write("%s\n" % section)
            keys = self.sections[section]
            keys.sort()
            for key in keys:
                fileobj.write("%s = %s\n" % (key, variables[key]))
            fileobj.write("\n")
            
    def save(self):
        "save(), if configuration changed, save it"
        if not self.variables.is_changed():
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
