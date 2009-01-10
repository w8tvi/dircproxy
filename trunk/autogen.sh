#!/bin/sh
# autogen.sh for dircproxy
#Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
#                        Francois Harvey <contact at francoisharvey dot ca>
			

if [ -f ./configure ]; then
	echo "No need to run autogen.sh, run autoreconf"
else
	aclocal
	autoheader
	automake --add-missing --copy
	autoconf
	./configure --enable-maintainer-mode ${1+"$@"}
fi
