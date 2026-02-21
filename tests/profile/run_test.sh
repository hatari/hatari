#!/bin/sh

usage () 
{
	echo "Usage: $0 <hatari> --disasm <ext|uae> [--mmu on] [other args]"
	echo "ERROR: $1!"
	exit 1
}


if [ $# -lt 1 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	usage "help"
fi

hatari=$1
shift
if [ ! -x "$hatari" ]; then
	usage "First parameter must point to valid hatari executable"
fi

disasm=$(echo "$@" | sed -E 's/^.*--disasm (uae|ext).*$/\1/')
case $disasm in
	ext|uae) ;;
	*) usage "valid '--disasm' argument missing"
esac
echo "$@" | grep -q -e " --mmu " && mmu="-mmu"

basedir=$(dirname "$0")
testdir=$(mktemp -d)

remove_temp() {
  rm -rf "$testdir"
}
trap remove_temp EXIT

export HATARI_TEST=profile
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

# location for test binary + symbols, that works with fake TOS & exits after a while
prog_dir="$basedir/../cpu"

# debugger script starting the breakpoint chain for profiling actions
start_ini="profile.ini"

# program + its TEXT symbols + debugger files => testdir
cp "$prog_dir"/int_test.* "$basedir"/*.ini "$testdir"

# add "cd" to first debugger file so that rest of them can be found
echo "cd $testdir -f" > "$testdir/$start_ini"
cat "$basedir/$start_ini" >> "$testdir/$start_ini"

# Fake TOS jumps directly to program code, it does not call
# GEMDOS HD Pexec().  Therefore this requires additional
# breakpoint chaining for program startup instead of just:
#   --symload exec --parse prg:<script>
HOME="$testdir" $hatari --log-level fatal --fast-forward on --sound off \
	--parse "$testdir/$start_ini" --tos none "$@" "$testdir/int_test.tos" \
	>> "$testdir/log.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Test FAILED, Hatari returned error status ${exitstat}."
	cat "$testdir/log.txt"
	exit 1
fi

compare="disasm-$disasm$mmu.txt"
cp "$basedir/$compare"    "$testdir/$compare.old"
cp "$testdir/profile.txt" "$testdir/$compare.new"

# remove insignificant differences from the comparison & new profile files
for f in "$testdir/$compare".*; do
	# remove Hatari version
	sed -i -e 's/^\(Hatari CPU profile\).*$/\1/' "$f"
	# for 030 MMU profile, replace cycle counts with instruction counts
	# as 030 emulation cycle-accuracy has not stabilized yet (changes
	# to i-cache misses & d-cache hits rates are assumed to be rare)
	if [ -n "$mmu" ]; then
		sed -i -e 's/% (\([0-9]\+\), [0-9]\+,/% (\1, \1,/' "$f"
	fi
done

# TODO: compare also hatari_profile post-processing output?
if ! diff -q "$testdir/$compare".*; then
	echo "-----------------------------------------------------"
	echo "Test log output:"
	cat "$testdir/log.txt"
	echo "-----------------------------------------------------"
	echo "Profiler output differences:"
	diff -u "$testdir/$compare".*
	echo "-----------------------------------------------------"
	echo "Test FAILED, profile output differs!"
	echo "=> replace with *.new, *if* change was valid."
	# use original profile so we have also track record of "insignificant" changes
	mv "$testdir/profile.txt" "$basedir/compare.new"
	exit 1
fi

echo "Test PASSED."
exit 0
