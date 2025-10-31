#!/bin/sh

usage () 
{
	echo "Usage: $0 <hatari> --disasm <ext|uae> [more hatari args]"
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
prog_dir="$basedir/../cycles"

# debugger script starting the breakpoint chain for profiling actions
start_ini="profile.ini"

# program and its (named) TEXT symbols
cp "$prog_dir/"cyccheck.* "$basedir/"*.ini "$testdir"

# "cd" so that rest of debugger files can be found
echo "cd $testdir -f" > "$testdir/$start_ini"
cat "$basedir/$start_ini" >> "$testdir/$start_ini"

HOME="$testdir" $hatari --log-level fatal --fast-forward on --sound off \
	--parse "$testdir/$start_ini" --tos none "$@" "$testdir/cyccheck.prg" \
	>> "$testdir/log.txt" 2>&1
exitstat=$?
if [ $exitstat -ne 0 ]; then
	echo "Test FAILED, Hatari returned error status ${exitstat}."
	cat "$testdir/log.txt"
	exit 1
fi

compare="disasm-$disasm.txt"
cp "$basedir/$compare"    "$testdir/$compare.old"
cp "$testdir/profile.txt" "$testdir/$compare.new"

# remove insignificant differences from the comparison & new profile files
for f in "$testdir/$compare".*; do
	# remove Hatari version
	sed -i 's/^\(Hatari CPU profile\).*$/\1/' "$f"
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
