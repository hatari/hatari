# Makefile to build the Atari test program and other required files
# for the TOS tester and to run it with minimal or fairly full
# set of options.

# default target is 'test', everything else gets build as its deps
all: test

# targets without corresponding file
.PHONY: clean test test-all


# tos/ subdir should be either symlink to where you have your TOS
# images, or real directory with symlinks to TOS images you want
# to test with this. Or TOSDIR variable needs to point where they are:
#   TOSDIR=/path/to/toses/ make

TOSDIR ?= tos

# TOSDIR should contain at least this!
BUILD_TOS = $(TOSDIR)/etos512k.img

# Hatari machine config for AHCC build
BUILD_CONFIG = -m --machine tt --tos $(BUILD_TOS)

# where the code & test programs are
DIR = disk

# AHCC related files expected for building by disk/ahcc-* hconsole scripts
AHCC_FILES = $(DIR)/ahcc_p.ttp $(DIR)/include $(DIR)/lib


GEMDOS_TEST   = $(DIR)/GEMDOS.PRG
GEMDOS_SCRIPT = $(DIR)/ahcc-gemdos
GEMDOS_DEP    = $(DIR)/gemdos.c $(DIR)/common.c $(DIR)/gemdos.prj $(AHCC_FILES) $(BUILD_TOS)

# build the full GEMDOS emu tester.
# requires:
# - EmuTOS, AHCC and installed Hatari
# before running make:
# - symlink etos512k.img and ahcc_p.ttp + its include & lib dirs
#   under $(DIR)/ subdir
$(GEMDOS_TEST): $(GEMDOS_DEP)
	$(RM) $(DIR)/*.O $(DIR)/*.MAP $(DIR)/*.tmp
	../../tools/hconsole/hconsole.py $(GEMDOS_SCRIPT) -- $(BUILD_CONFIG) $(DIR)
	[ -f $(DIR)/GEMDOS.O ] && [ \! -f $(DIR)/ldfile.tmp ] # verify compiling & linking succeeded


MINIMAL_TEST   = $(DIR)/MINIMAL.PRG
MINIMAL_SCRIPT = $(DIR)/ahcc-minimal
MINIMAL_DEP    = $(DIR)/minimal.c $(DIR)/common.c $(DIR)/minimal.prj $(AHCC_FILES) $(BUILD_TOS)

# build the minimal tester
$(MINIMAL_TEST): $(MINIMAL_DEP)
	$(RM) $(DIR)/*.O $(DIR)/*.MAP $(DIR)/*.tmp
	../../tools/hconsole/hconsole.py $(MINIMAL_SCRIPT) -- $(BUILD_CONFIG) $(DIR)
	[ -f $(DIR)/MINIMAL.O ] && [ \! -f $(DIR)/ldfile.tmp ] # verify compiling & linking succeeded


clean:
	$(RM) $(DIR)/*.O $(DIR)/*.MAP $(DIR)/*.tmp


# create blank DD floppy image
blank-a.st.gz:
	dd if=/dev/zero of=blank-a.st bs=1024 count=720
	mformat -a -t 80 -h 2 -n 9 -i blank-a.st ::
	gzip blank-a.st


# create 360KB (single side) test floppy that autoruns test program using *.INF file.
# requires:
# - mformat & mcopy from mtools
bootdesk.st.gz: $(MINIMAL_TEST) $(DIR)/TEXT floppy/*.INF
	dd if=/dev/zero of=bootdesk.st bs=1024 count=360
	mformat -a -t 80 -h 1 -n 9 -i bootdesk.st ::
	MTOOLS_NO_VFAT=1 mcopy -i bootdesk.st -spmv $+ ::
	$(RM) $@
	gzip bootdesk.st

# create 360KB (single side) test floppy that autoruns test program from auto/
# as very old TOS versions don't like the *.INF file autorun feature.
# requires:
# - mformat, mcopy & mmd from mtools
bootauto.st.gz: $(MINIMAL_TEST) $(DIR)/TEXT
	dd if=/dev/zero of=bootauto.st bs=1024 count=360
	mformat -a -t 80 -h 1 -n 9 -i bootauto.st ::
	MTOOLS_NO_VFAT=1 mmd -i bootauto.st ::AUTO
	MTOOLS_NO_VFAT=1 mcopy -i bootauto.st -pmv $(DIR)/TEXT ::
	MTOOLS_NO_VFAT=1 mcopy -i bootauto.st -pmv $(MINIMAL_TEST) ::AUTO
	$(RM) $@
	gzip bootauto.st


# optional 16MB HD image for EmuTOS/ACSI testing without HD drivers
# converts floppy desktop infos for HD (A: -> C:)
hd.img: $(MINIMAL_TEST) $(DIR)/TEXT floppy/*.INF
	mkdir tmp
	cp -a $(MINIMAL_TEST) $(DIR)/TEXT tmp/
	for i in floppy/*.INF; do sed -e 's/A:/C:/g' < $$i > tmp/$${i##*/}; done
	../../tools/atari-hd-image.sh 16 $@ LABEL tmp
	$(RM) -r tmp


# requires:
# - Building of floppies & GEMDOS_TEST to have succeeded
# - Latest Hatari to be installed, or to run this with something like:
#   PATH=../../build/src:$PATH make
test: blank-a.st.gz bootauto.st.gz bootdesk.st.gz $(GEMDOS_TEST)
	./tos_tester.py --disks floppy,gemdos --graphics mono --memsizes 4 --machines ste $(BUILD_TOS)

# run all default tests
test-full: blank-a.st.gz bootauto.st.gz bootdesk.st.gz
	./tos_tester.py $(TOSDIR)/*.img
