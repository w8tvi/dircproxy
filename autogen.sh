#!/bin/sh
# autogen.sh for dircproxy
# Scott James Remnant <scott@netsplit.com>

if [ -f ./configure ]; then
	echo "No need to run autogen.sh, run autoreconf"
else
	aclocal
	autoheader
	automake --add-missing --copy
	autoconf
	./configure --enable-maintainer-mode ${1+"$@"}
fi
