#!/bin/sh
# autogen.sh for dircproxy
# Scott James Remnant <scott@netsplit.com>

# gettextize
aclocal-1.4
autoheader2.13
automake-1.4 --add-missing --copy
autoconf2.13
./configure $@
