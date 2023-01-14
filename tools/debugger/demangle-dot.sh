#!/bin/sh

# check tools
if [ -z "$(which c++filt)" ]; then
	echo "ERROR: c++filt missing!"
	exit 1
fi

if [ "${1%.dot}" = "$1" ]; then
	echo "ERROR: '$1' is not a GraphViz .dot file!"
	exit 1
fi

if [ ! -f "$1" ]; then
	echo "ERROR: '$1' input file missing!"
	exit 1
fi

# prepare mangled symbols for c++filt:
# - remove extra '_' prefix
# - remove extra .isra.<number> postfixes
# - add spaces around symbols as required by c++filt
# afterwards shorten C++ symbol names for viewing:
# - remote template args
# - remove method args
#   - unfortunately this removes also some info from graph title
# - remove [clone xxx] info
# - remove part of thunking info
sed \
  -e 's/__Z/_Z/' \
  -e 's/\.isra[.0-9]*/ /g' \
  -e 's/\\n/ \\n /g' \
  "$1" | c++filt |\
sed \
  -e 's/<[^>]*>/<>/g' \
  -e 's/([a-zA-Z][^)%]*)/()/g' \
  -e 's/\[clone[^]]*\]//g' \
  -e 's/thunk to//g' \
