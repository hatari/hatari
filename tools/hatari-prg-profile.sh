#!/bin/sh

# or override these with paths to non-installed Hatari version
hatari=$(which hatari)
profile=$(which hatari_profile)
gst2ascii=$(which gst2ascii)

# show profile report summary: $1=report
show_overview ()
{
	awk '
	/^CPU profile/ { out=1 }
	/^Time / { print $0 "\n"; out=0 }
	/^Used / { out=1 }
	/^Instruction/ { exit }
	/^ *0\./ { if(out) { print; exit } }
	{ if (out) print }
	' "$1"
	echo
}

# post_process profile: <symbols file> <profile file> <report file>
post_process ()
{
	symbols=$1
	input=$2
	output=$3

	echo "$profile -stp -r $symbols --limit 0.1 $input > $output"
	$profile -stp -r "$symbols" --limit 0.1 "$input" > "$output"

	echo
	ls -l "$input" "$output"
}

# show script help & exit: $1=errmsg
error_exit ()
{
	name=${0##*/}
	cat <<EOF 1>&2

Run given Atari program from GEMDOS HD with Hatari and profile its
execution until program calls specified GEMDOS opcode. Save the
profile data, post-processes it, and show overview of the generated
profile report.

Usage:
	$name <path> <opcode> [options]

Arguments:
	path    - Path to Atari program to profile
	opcode  - 2-digit GEMDOS opcode in hex
	options - Extra options to provide for Hatari

Examples:
	# profile program until it terminates with Pterm() (0x4C):
	$name ./hello.tos 4C

	# profile OPL2 DSP midiplayer until Fcreate() (0x3C),
	# under Falcon emulation:
	$name ./f030mid.tos 3C --machine falcon -s 4

ERROR: $1!

EOF
	exit 1
}

# process command line args
if [ $# -lt 2 ]; then
	error_exit "not enough arguments"
fi

prg=$1
if [ ! -e "$prg" ]; then
	error_exit "specified '$prg' Atari program to profile, is missing"
fi
shift

opcode=$1
if [ ${#opcode} -ne 2 ]; then
	error_exit "specified '$opcode' is not two-digit hex number"
fi
shift

hatari_args=$*

# verify required tools exist
if [ ! -x $hatari ]; then
	error_exit "'hatari' not found, is it in PATH?"
fi
if [ ! -x $profile ]; then
	error_exit "'hatari_profile' [.py] not found, is it in PATH?"
fi
if [ ! -x $gst2ascii ]; then
	error_exit "'gst2ascii' not found, is it in PATH?"
fi

# program dir
dir=${prg%/*}
if [ ! -d "$dir" ]; then
	error_exit "Atari program dir '$dir' missing"
fi

# program name without path
prg=${prg##*/}

# program name without extension
base=$(echo "$prg" | sed -E 's/[.](app|gtp|prg|tos|ttp|APP|GTP|PRG|TOS|TTP)//')

# check for / extract program symbols required by post-processing
syms="$dir/$base.sym"
if [ ! -f "$syms" ]; then
	echo "Warning: '$syms' symbols file missing, extracting them from the program for profile post-processing"
	# skip non-TEXT section symbols
	if ! $gst2ascii -a -b -d "$dir/$prg" > "$syms"; then
		error_exit "'$prg' does not include symbols, cannot profile"
	fi
	if grep -q " _ZN" "$syms"; then
		echo "Symbols seem to be C++ / mangled, demangling with host 'c++filt' (in binutils)..."
		c++filt < "$syms" > "$syms.tmp"
		mv "$syms.tmp" "$syms"
	fi
else
	echo "Using pre-existing symbols file '$syms'"
fi

# debugger scripts for profiling
start="profile-start.ini"
save="profile-save.ini"

# profile data save file
data="profile-data.txt"

# start program (CPU side) profiling, assumes symbols are already loaded)
# invoke second debugger script when program calls specified GEMDOS opcode
cat > "$start" << EOF
profile on
b GemdosOpcode = 0x$opcode :trace :file ${save##*/}
EOF

# save collected profile & set Hatari quit flag / exit value
cat > "$save" << EOF
profile save $data
quit 0
EOF

# show debugger scripts
head "$start" "$save"

# remove old profile data to later check whether it's creation succeeded
rm "$data"

# profile the program
# shellcheck disable=SC2086
$hatari --trace os_base,event --symload exec \
  --parse "prg:$start"  $hatari_args  "$dir/$prg"

# check that profiling succeeded to end
if [ ! -f "$data" ]; then
	error_exit "profiling failed, profile data file '$data' missing"
fi

# remove debugger scripts
rm "$start" "$save"

# generated report
report="profile-report.txt"
 
# post-process saved profile data
post_process "$syms" "$data" "$report"
echo

# show overview of the generated report
show_overview "$report"
