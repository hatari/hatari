#!/usr/bin/python2
#
# for Python3, remove sorting cmp function
"""
Script to copy Atari files with (potentially) too long names from
source directory into target directory, using in latter file names
clipped to 8+3 characters.

Clipping is done like Atari GEMDOS functions would do it, so that
those file names can then be moved/copied to Atari media from another
OS.

If original long names work with Hatari GEMDOS HD emulation, the
clipped names should work with normal TOS on real harddisk (image)!
"""
import sys, os, shutil

def debug(msg):
    sys.stderr.write("%s\n" % msg)

def error_exit(msg):
    name = os.path.basename(sys.argv[0])
    debug("\nUsage: %s <source dir> <target dir>" % name)
    debug(__doc__)
    debug("ERROR: %s!\n" % msg)
    sys.exit(1)

newnames = {}

def check_conflicts(srcdir, dstdir):
    # how much to clip from paths
    srcskip = len(srcdir)+1
    dstskip = len(dstdir)+1
    print "\nNames that aren't unique:"
    conflicts = False
    for key,names in newnames.items():
        if len(names) > 1:
            conflicts = True
            print "- %s: %s" % (key[dstskip:], [name[srcskip:] for name in names])
    if not conflicts:
        print "- none, all OK!"

def hash_names(original, newname):
    if newname not in newnames:
        newnames[newname] = []
    newnames[newname].append(original)

def clip_name(name):
    dot = name.rfind('.')
    if dot >= 0:
        base = name[:dot]
        ext = name[dot+1:]
        name = "%s.%s" % (base[:8], ext[:3])
    else:
        name = name[:8]
    # TODO: map non-ASCII characters
    return name.upper()

def dirs_last(a, b):
    # in alphabetical order, dirs last
    adir = os.path.isdir(a)
    bdir = os.path.isdir(b)
    if adir and bdir:
        return cmp(a, b)
    if adir:
        return 1
    if bdir:
        return -1
    return cmp(a, b)

def convert_dir(srcdir, dstdir):
    print "\n%s/ -> %s/:" % (srcdir, dstdir)
    try:
        os.mkdir(dstdir)
    except OSError:
        debug("ERROR: directory creation failed, name conflict???")
        return
    # directory sorting requires full names
    dircontents = [os.path.join(srcdir, x) for x in os.listdir(srcdir)]
    for original in sorted(dircontents, cmp=dirs_last):
        origname = os.path.basename(original)
        clipname = clip_name(origname)
        newname = os.path.join(dstdir, clipname)
        hash_names(original, newname)
        if os.path.isdir(original):
            convert_dir(original, newname)
        else:
            print "- %s -> %s" % (origname, clipname)
            try:
                shutil.copyfile(original, newname)
            except IOError:
                debug("ERROR: file copy failed, name conflict (with read-only file)???")
                continue
            shutil.copystat(original, newname)

def main(args):
    if len(args) != 3:
        error_exit("too few arguments")
    srcdir = args[1]
    dstdir = args[2]
    if not os.path.isdir(srcdir):
        error_exit("source directory '%s' doesn't exist" % srcdir)
    if os.path.isdir(dstdir):
        error_exit("target directory '%s' exists, remove it to continue" % dstdir)
    if srcdir[-1] == os.path.sep:
        srcdir = srcdir[:-1]
    if dstdir[-1] == os.path.sep:
        dstdir = dstdir[:-1]
    if dstdir.startswith(srcdir+os.path.sep):
        error_exit("target directory '%s' is inside source directory '%s'" % (srcdir, dstdir))        
    convert_dir(srcdir, dstdir)
    check_conflicts(srcdir, dstdir)

if __name__ == '__main__':
    main(sys.argv)
