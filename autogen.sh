#!/bin/sh
# autogen.sh for dircproxy
# Scott James Remnant <scott@netsplit.com>

# gettextize
aclocal
autoheader
automake --add-missing --copy
autoconf
./configure $@
