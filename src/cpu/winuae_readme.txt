		The CPU core in this directory is based on
			WinUAE 5.1.0 beta1 (2023/09/17)


To update to a newer WinUAE's version, a diff should be made between
WinUAE 5.1.0 beta1 sources and the newer sources, then the resulting patch
should be applied to the files in this directory.

Most files are similar to WinUAE's ones, so patches should apply in
most cases ; except custom.c and memory.c which contain too much
Amiga specific code and were trimmed down and might need manual editing
to apply patches. newcpu.c contains many Hatari specific code, so patches
might need manual editing too.

If you update to a newer WinUAE's version, also update the version at the
top of this readme file to keep track of the reference version used
for the CPU core.

