#!/bin/sh
# autogen.sh for dircproxy
# Francois Harvey <fharvey at securiweb dot net>

if [ -f ./configure ]; then
	echo "No need to run autogen.sh, run autoreconf"
else
	aclocal
	autoheader
	automake --add-missing --copy
	autoconf
	./configure --enable-maintainer-mode ${1+"$@"}
fi
