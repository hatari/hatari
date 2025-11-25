#!/bin/sh

if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: $0 <hatari>"
	exit 1
fi

hatari=$1
if [ ! -x "$hatari" ]; then
	echo "First parameter must point to valid hatari executable."
	exit 1
fi

testdir=$(mktemp -d)
log="$testdir/out.txt"
empty="$testdir/empty.txt"
missing="$testdir/missing-file"

export HATARI_TEST=options
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
unset TERM


failed=0
wrong_fails=0
wrong_fail_values=0
wrong_successes=0

# Check whether Hatari exit value was expected.
# args: <exit value> <expected value>
check_exit ()
{
	exitval=$1
	expected=$2
	if [ "$exitval" -eq "$expected" ]; then
		return
	fi

	# unexpected
	if [ "$expected" -eq 0 ]; then
		echo "FAIL: Unexpected option fail for '$args' (exit value: $exitval)"
		wrong_fails=$((wrong_fails+1))
	elif [ "$exitval" -eq 0 ]; then
		echo "FAIL: Unexpected option success for '$args' (zero exit value)"
		wrong_successes=$((wrong_successes+1))
	else
		echo "FAIL: Unexpected fail value for '$args' (exit value: $exitval)"
		wrong_fail_values=$((wrong_fail_values+1))
	fi
	failed=$((failed+1))
}


# Final option to make Hatari quit with zero exit value
# when options preceeding it were parsed OK.
exit0="--version"

# Test space separated option lists. For each item,
# convert '|' chars to space before passing it to Hatari,
# and check whether Hatari exit value was expected.
# Args: <expected exit value> <options>
test_options ()
{
	expected=$1
	for option in $2; do
		if [ -z "$option" ]; then
			continue
		fi
		args=$(echo "$option" | tr '|' ' ')
		echo "$args"

		touch "$empty"
		# word splitting is required
		# shellcheck disable=SC2086
		$hatari $args $exit0 >> "$log" 2>&1
		check_exit "$?" "$expected"
		rm "$empty"
	done
}


# Hatari options without arguments
narg_opts=$($hatari --help | awk '
/ --[a-z][-a-z]* [^<]/ { printf("%s|",$1) }
')

echo
echo "Check no-argument options, all in one go..."
test_options 0 "$narg_opts"


# different event prefixes & bool option values
bool_vals=$(
for i in boot:ON inf:OFF prg:ON TRUE FALSE true false 1 0; do
	printf "|--fast-forward|%s|" $i
done)

echo
echo "Check event prefixes + <bool> value alternatives..."
test_options 0 "$bool_vals"


# all hatari <bool> options, each set on+off+on
bool_opts=$($hatari --help | awk '
/--[^<]*<bool>/ { printf("%s|on|%s|off|%s|on|", $1, $1, $1) }
')

echo
echo "Check <bool> options, all in one go..."
test_options 0 "$bool_opts"


# All Hatari <dir> + <file> options, using (existing)
# <testdir>, or <empty> file under it.
# Skip options that won't work with these (or together).
file_opts=$($hatari --help | awk '
/disk-b/ { next } # fails if both drives have same file
/fifo/   { next }
/socket/ { next }
/--[^<]*<dir>/  { printf("%s|'"$testdir"'|", $1) }
/--[^<]*<file>/ { printf("%s|'"$empty"'|", $1) }
')

echo
echo "Check <dir> / <file> options, all in one go..."
test_options 0 "$file_opts"


# All Hatari <dir> + <file> options that expect arg path/file to exist
# (skip options causing file to be created if it does not exist).
file_fail_opts=$($hatari --help | awk '
/-out /         { next }
/--printer/     { next }
/--avi-file/    { next }
/--cmd-fifo/    { next }
/--log-file/    { next }
/--trace-file/  { next }
/--[^<]*<dir>/  { printf("%s|'"$missing"'\n", $1) }
/--[^<]*<file>/ { printf("%s|'"$missing"'\n", $1) }
')

echo
echo "Check expected <dir> / <file> option fails, when path does not exist..."
test_options 1 "$file_fail_opts"


# Valid values for options with other than bool/dir/file arg types:
# $ hatari --help |\
#   grep -v -e '<bool>' -e '<dir>' -e '<file>' |\
#   grep ' --[-a-z0-9]* <'
# Note: test function converts '|' chars to spaces.
other_opts="
--country|us|--layout|uk|--language|de|--rtc-year|2025|
--monitor|tv|--tos-res|low|--video-timing|random|--zoom|2|--avi-vcodec|bmp|
--joy0|real|--joy1|keys|--joy2|none|--joy3|real|--joy4|keys|--joy5|none|
--protect-floppy|on|--protect-hd|off|--gemdos-drive|C|--gemdos-drive|skip|
--scsi|1=$empty|--acsi|0=$empty|--scsi-ver|0=1|--scsi-ver|1=2|
--ide-swap|off|--ide-swap|0=on|--ide-swap|1=auto|
--fpu|68881|--fpu|68882|--fpu|internal|
--machine|st|--machine|megaste|--machine|tt|--machine|falcon|--dsp|off|
--sound|50066|--sound-buffer-size|100|--ym-mixing|model|
--sound|6000|--sound-buffer-size|0|--ym-mixing|table|--sound|off|
--debug-except|all|--debug-except|bus,chk,dsp|--debug-except|none|
--symload|exec|--symload|debugger|--symload|off|--lilo|anything-goes|
--trace|none|--trace|os_base,-gemdos,+xbios,+aes|--trace|none|
--disasm|uae|--disasm|ext|--disasm|0x7f|--disasm|0|
--log-level|debug|--alert-level|fatal|--log-level|error|--alert-level|info|
"

echo
echo "Check options taking other arg types, few at the time..."
test_options 0 "$other_opts"


# Negative tests need to be done one-by-one,
# to verify that each one actually fails.
fail_opts="
--none
--country|none|
--monitor|none|
--tos-res|superhigh|
--zoom|0|
--video-timing|none|
--vdi|none|
--vdi|foo:bar|
--avi-vcodec|none|
--screenshot-format|none|
--joy8|off|
--protect-floppy|none|
--protect-hd|none|
--gemdos-case|gray|
--gemdos-time|fun|
--gemdos-drive|A|
--acsi|0=$missing|
--scsi|1=$missing|
--fpu|imaginary|
--machine|none|
--dsp|missing|
--sound|great|
--ym-mixing|none|
--debug-except|exceptional|
--symload|never|
--disasm|none|
--trace
--trace|foo:none|
--trace|foo,bar|
--log-level|none|
--alert-level|none|
"

echo
echo "Check expected failures..."
test_options 1 "$fail_opts"


echo
if [ $failed -ne 0 ]; then
	echo "FAIL, unexpected exit values for $failed tests:"
	echo "- $wrong_fails unexpected failure(s)"
	echo "- $wrong_fail_values unexpected fail value(s)"
	echo "- $wrong_successes unexpected success(es)"
	#cat "$log"
else
	echo "Tests PASSED."
fi

rm -rf "$testdir"
exit $failed
