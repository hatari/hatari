#!/bin/sh
#
# Script for screenshot report:
# - compares images in current dir to references images
# - reports images that are in reference subdir,
#   but not in current dir and images for which
#   there are no reference images.
#
# Copyright (C) 2012 by Eero Tamminen <oak at helsinkinet fi>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

if [ -z "$(ls *.png *.bmp 2>/dev/null)" ]; then
	echo "ERROR: no screenshots!"
	echo
	echo "Run this from a directory with PNG/BMP screenshots"
	echo "to get HTML report showing them, which of them differ"
	echo "from the similarly named screenshots in 'reference'"
	echo "directory etc."
	exit 1
fi

report="screenshot-report.html"
refdir=reference
difdir=difference

title="Screenshot comparison report"
cat > $report << EOF
<html>
  <title>$title</title>
<body>
<h1>$title</h1>
<p>Contents:
<ul>
  <li><a href="#missing">Missing screenshots</a>
  <li><a href="#mismatched">Mismatched screenshots</a>
  <li><a href="#matching">Matching screenshots</a>
  <li><a href="#new">New screenshots</a>
</ul>
EOF


get_name ()
{
	name=$1
	name=${name%.png}
	name=${name%.bmp}
}

missing=""
mismatched=""
matching=""
new=""

# images in reference dir, but not in current one
for refimg in $(ls $refdir/*.png $refdir/*.bmp 2>/dev/null); do
	img=${refimg##*/}
	if [ \! -f "$img" ]; then
		missing="$missing $img"
		continue
	fi
done

echo "<a name=missing></a><h2>Missing screenshots</h2>" >> $report
if [ -z "$missing" ]; then
	echo "None." >> $report
else
	echo "<ul>" >> $report
	for img in $missing; do
		get_name $img
		echo "<li>$name" >> $report
	done
	echo "</ul>" >> $report
	echo "Missing:"
	echo $missing
fi

# images that don't match the references ones
for img in $(ls *.png *.bmp 2>/dev/null); do
	refimg=$refdir/$img
	if [ \! -f "$refimg" ]; then
		new="$new $img"
		continue
	fi
	cmp $img $refimg > /dev/null
	if [ $? -eq 0 ]; then
		matching="$matching $img"
		continue
	fi
	mismatched="$mismatched $img"
done

echo "<a name=mismatched></a><h2>Mismatched screenshots</h2>" >> $report
if [ -z "$mismatched" ]; then
	echo "None." >> $report
else
	echo "<p>Images which PNG/BMP screenshot file isn't exactly the same as the reference file" >> $report
	echo "(the image content could still be identical although the files aren't, due to changes e.g. in compression)." >> $report
	#echo "Left = new image, middle = difference, right = reference image:" >> $report
	echo "<p>Left = new image, right = reference image:" >> $report
	mkdir -p $difdir
	for img in $mismatched; do
		refimg=$refdir/$img
		get_name $img
		diff=$difdir/$name.png
		# disable compare usage, it gets stuck with TT mono images
		#compare $img $refimg $diff
		#if [ $? -eq 0 ]; then
		#	echo "<p><div align=center><img src=$img><img src=$diff><img src=$refimg><br>$name</div>" >> $report
		#else
			echo "<p><div align=center><img src=$img><img src=$refimg><br>$name</div>" >> $report
		#fi
	done
fi

echo "<a name=matching></a><h2>Matching screenshots</h2>" >> $report
if [ -z "$matching" ]; then
	echo "None." >> $report
else
	echo "<ul>" >> $report
	for img in $matching; do
		get_name $img
		echo "<li>$name" >> $report
	done
	echo "</ul>" >> $report
fi

echo "<a name=new></a><h2>New screenshots</h2>" >> $report
if [ -z "$new" ]; then
	echo "None." >> $report
else
	msg="<p>These are candidates to be moved to the '$refdir' directory:"
	echo "<p>$msg" >> $report
	for img in $new; do
		get_name $img
		echo "<p><div><img src=$img alt=$img><br>$name</div>" >> $report
	done
	echo $msg
	echo $new
fi

echo "</body></html>" >> $report

# show the report in the preferred HTMl viewer
xdg-open $report
