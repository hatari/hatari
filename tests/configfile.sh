#!/bin/sh
#
# Check whether loading and saving of config files works as expected

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari>"
	exit 1;
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1;
fi;

testdir=$(mktemp -d)
if [ "$(uname -s)" = "Darwin" ]; then
	mkdir -p "${testdir}/Library/Application Support"
	cfgfile="${testdir}/Library/Application Support/Hatari/hatari.cfg"
else
	cfgfile="$testdir"/.config/hatari/hatari.cfg
fi

remove_temp() {
  rm -rf "$testdir"
}
trap remove_temp EXIT

keymap="$testdir"/keymap.txt
echo "# Temp file" > "$keymap"

acsifile="$testdir"/acsi.img
dd if=/dev/zero of="$acsifile" bs=512 count=1000 2> /dev/null
scsifile="$testdir"/scsi.img
dd if=/dev/zero of="$scsifile" bs=512 count=1000 2> /dev/null
idefile="$testdir"/ide.img
dd if=/dev/zero of="$idefile" bs=512 count=1000 2> /dev/null

export HATARI_TEST=configfile
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
export HOME="$testdir"
unset TERM

$hatari --log-level fatal --sound off --tos none --confirm-quit false \
	--machine tt --cpulevel 4 --fpu internal --cpuclock 16 --drive-led 0 \
	--monitor tv --frameskips 3 --mousewarp off --statusbar FALSE \
	--disasm uae --joy0 keys --keymap "$keymap" --crop 1 --fast-boot 1 \
	--protect-floppy auto --gemdos-case upper --acsi 3="$acsifile" \
	--scsi 5="$scsifile" --ide-master "$idefile" --patch-tos off \
	--vdi 1 --rs232-out "$testdir"/serial-out.txt --rs232-in /dev/null \
	--printer /dev/zero --avi-fps 60 --memsize 0 --alert-level fatal \
	--saveconfig >"$testdir/out.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari with parameters FAILED. Status = ${exitstat}."
	cat "$testdir/out.txt"
	exit 1
fi

echo "############################## Log file: ##############################"
cat "$cfgfile"

# Check whether the command line options have been included in the config file:
echo "#################### Looking for expected settings: ###################"
grep "nTextLogLevel = 0" "$cfgfile" || exit 1
grep "bEnableSound = FALSE" "$cfgfile" || exit 1
grep "bConfirmQuit = FALSE" "$cfgfile" || exit 1
grep "nModelType = 4" "$cfgfile" || exit 1
grep "nCpuLevel = 4" "$cfgfile" || exit 1
grep "nCpuFreq = 16" "$cfgfile" || exit 1
grep "bUseExtVdiResolutions = TRUE" "$cfgfile" || exit 1
grep "bShowDriveLed = FALSE" "$cfgfile" || exit 1
grep "nMonitorType = 3" "$cfgfile" || exit 1
grep "nFrameSkips = 3" "$cfgfile" || exit 1
grep "bMouseWarp = FALSE" "$cfgfile" || exit 1
grep "bShowStatusbar = FALSE" "$cfgfile" || exit 1
grep "bDisasmUAE = TRUE" "$cfgfile" || exit 1
grep "nJoystickMode = 2" "$cfgfile" || exit 1
grep "szMappingFileName = $keymap" "$cfgfile" || exit 1
grep "nMemorySize = 512" "$cfgfile" || exit 1
grep "nWriteProtection = 2" "$cfgfile" || exit 1
grep "nGemdosCase = 1" "$cfgfile" || exit 1
grep "sDeviceFile3 = $acsifile" "$cfgfile" || exit 1
grep "sDeviceFile5 = $scsifile" "$cfgfile" || exit 1
grep "sDeviceFile0 = $idefile" "$cfgfile" || exit 1
grep "bPatchTos = FALSE" "$cfgfile" || exit 1
grep "bEnableRS232 = TRUE" "$cfgfile" || exit 1
grep "szOutFileName = $testdir/serial-out.txt" "$cfgfile" || exit 1
grep "szInFileName = /dev/null" "$cfgfile" || exit 1
grep "bEnablePrinting = TRUE" "$cfgfile" || exit 1
grep "szPrintToFileName = /dev/zero" "$cfgfile" || exit 1
grep "bCrop = TRUE" "$cfgfile" || exit 1
grep "AviRecordFps = 60" "$cfgfile" || exit 1
grep "nAlertDlgLogLevel = 0" "$cfgfile" || exit 1

echo "######################################################################"

# Now check that we can load and save the same config file again:

touch "$testdir"/test.cfg
cat > "$testdir"/commands.txt << EOF
setopt -c $testdir/test.cfg
setopt --saveconfig
EOF

$hatari --tos none --parse "$testdir"/commands.txt >"$testdir/out.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Running hatari -c ${testdir}/test.cfg FAILED. Status ${exitstat}."
	cat "$testdir"/out.txt
	exit 1
fi

if ! diff -q "$cfgfile" "$testdir"/test.cfg ; then
	echo "Test FAILED, config files differs:"
	diff -u "$cfgfile" "$testdir"/test.cfg
	exit 1
fi

echo "Test PASSED."
exit 0
