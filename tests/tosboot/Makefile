# Makefile to build the Atari test program and other required files
# for the TOS tester and to run it with minimal or fairly full
# set of options.

DIR = disk

# requires:
# - etos512k.img or symlink to it under $(DIR)/ subdir
# - Latest Hatari to be installed, or to run this with something like:
#   PATH=../../build/src:$PATH make
test: blank-a.st.gz floppy.st.gz
	./tos_tester.py --disks floppy,gemdos --graphics mono --memsizes 4 --machines ste $(DIR)/etos512k.img


TOSDIR ?= tos

# requires additionally:
# - tos/ subdir to be either symlink to where you have your TOS
#   images, or read directory with symlinks to TOS images you
#   want to test with this, or TOSDIR to point where they are:
#   TOSDIR=/path/to/toses/ make
test-full: blank-a.st.gz floppy.st.gz
	./tos_tester.py $(TOSDIR)/*.img


PRG = $(DIR)/GEMDOS.PRG
DEP = $(DIR)/gemdos.c $(DIR)/gemdos.prj

# build the test program.
# requires:
# - EmuTOS, AHCC and installed Hatari
# before running make:
# - symlink etos512k.img and ahcc_p.ttp + its include & lib dirs
#   under $(DIR)/ subdir
$(PRG): $(DEP)
	../../tools/hconsole/hconsole.py $(DIR)/ahcc-build -- -m --machine tt --tos $(DIR)/etos512k.img $(DIR)
	$(RM) $(DIR)/GEMDOS.O $(DIR)/GEMDOS.MAP


# create blank DD floppy image
blank-a.st.gz:
	dd if=/dev/zero of=blank-a.st bs=1024 count=720
	mformat -a -t 80 -h 2 -n 9 -i blank-a.st ::
	gzip blank-a.st


# create 360KB (single side) test floppy that autoruns $(PRG).
# requires:
# - mformat & mcopy from mtools
floppy.st.gz: $(PRG) $(DIR)/text $(DIR)/test floppy/*.INF
	dd if=/dev/zero of=floppy.st bs=1024 count=360
	mformat -a -t 80 -h 1 -n 9 -i floppy.st ::
	MTOOLS_NO_VFAT=1 mcopy -i floppy.st -spmv $(PRG) $(DIR)/text $(DIR)/test floppy/*.INF ::
	$(RM) $@
	gzip floppy.st


# optional 16MB HD image for EmuTOS/ACSI testing without HD drivers
# converts floppy desktop infos for HD (A: -> C:)
hd.img: $(PRG) $(DIR)/text $(DIR)/test floppy/*.INF
	mkdir tmp
	cp -a $(PRG) $(DIR)/text $(DIR)/test tmp/
	for i in floppy/*.INF; do sed 's/A:/C:/g' < $$i > tmp/$${i##*/}; done
	../../tools/atari-hd-image.sh 16 $@ LABEL tmp
	$(RM) -r tmp
