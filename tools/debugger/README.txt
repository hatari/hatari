This directory contains helper & post-processing tools and legacy
scripts to be used with Hatari debugger symbol handling and profiling.

They are...


Scripts for converting different ASCII symbols (name/address) formats
to a nm-style ASCII symbols format understood by the Hatari debugger:

- ahcc-symbols-convert.sh
- devpac3-symbols-convert.sh
- dsp-lod-symbols-convert.sh

NOTE: Normally you should set AHCC & DevPac to generate binaries with
debug symbols (as explained in debugger documentation) instead of
using these scripts.  DSP LOD symbols you need only when profiling
Falcon DSP LOD code.


Tool for outputting binary symbols data from Atari programs as ASCII,
to edit it before feeding it to Hatari debugger:

- gst2ascii.1
- gst2ascii.c

Typical use-cases for wanting this are profiling related:

* Manual (or automated) removal of symbols that make profiler output
  messy, or slow it down too much, such as symbols for loop entry
  points

* Binary is missing a symbol(s) / address(es) you're interested about,
  and you want to add it manually (e.g. old MiNTlib builds had stripped
  out symbols for time related functionality that took a lot of cycles)


Hatari profiler output post-processing and call-graph generation:
- hatari_profile.1
- hatari_profile.py


Post-processing tool providing analysis data for optimizing I/O waits:
- hatari_spinloop.py

(= what you can do after you've optimized everything else profiler
tells about CPU & DSP usage.)


Legacy script for cleaning irrevant symbols out of 'nm' tool output
(before gst2ascii and Hatari debugger supported binary symbols data):
- nm-symbols-cleanup.sh
