#!/bin/sh
# autogen.sh for dircproxy
# Scott James Remnant <scott@netsplit.com>

# gettextize
aclocal-1.6
autoheader
automake-1.6 --add-missing --copy
autoconf
./configure --enable-maintainer-mode "$@"
