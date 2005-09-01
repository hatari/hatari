#!/bin/sh

# check arguments
if [ -z "$1" ]; then
	echo "usage: $0 <html file>"
	echo
	echo "Adds header anchors to the file and then outputs an corresponding index."
	exit 1
fi
if [ \! -f "$1" ]; then
	echo "ERROR: file '$1' not found"
	exit 1
fi
doc=$1
new=$doc.new
tmp=$doc.tmp

# remove existing header anchors
sed 's%<a name="[^"]*"></a><h%<h%' $doc > $tmp

# add an anchor before each of the headings
# assumes valid, single line HTML headers without "'s
sed 's%\(<[hH][0-9]>\)\([^<]*\)\(</[hH][0-9]>\)%<a name="\2"></a>\1\2\3%' $tmp > $new
rm $tmp

# ask user whether he wants the new changes
echo "Changes"
echo "======="
diff -u $doc $new
while true; do
	echo "Replace original (y/n)?"
	read answer
	
	if [ "$answer" = 'y' ]; then
		mv $new $doc
		echo "replaced."
		break
	elif [ "$answer" = 'n' ]; then
		rm $new
		echo "undo."
		break
	fi
done

# output index
idx=$doc.idx
echo "<ul>" > $idx
grep '<a name="' $doc|sed 's%^.*<a name="\([^"]*\)".*$%  <li><a href="#\1">\1</a>%' >> $idx
echo "</ul>" >> $idx

echo "---------------------------------------------------------"
cat $idx
echo "---------------------------------------------------------"
echo "Above listed index for the '$doc' file is in file '$idx'."
