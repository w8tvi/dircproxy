#!/bin/sh
# Script to run from crontab to check whether dircproxy is still running,
# and if not, restart it
# 
# You will need to set the 'pid_file' option in your .dircproxyrc
# 
# To use, set the following in crontab
#   */10 * * * *   cronchk.sh
#  or
#   */10 * * * *   cronchk.sh /path/to/dircproxyrc

# command to run dircproxy
DIRCPROXY=dircproxy

# config file can be specified as the first argument
CONFFILE=$HOME/.dircproxyrc
if test -n "$1"; then
	CONFFILE=$1
fi

# look for the pid_file directive
PIDFILE=`grep ^pid_file $CONFFILE | sed 's/^pid_file[ 	"]*//' | sed 's/"*$//'`
PIDFILE=`eval echo \`echo $PIDFILE | sed 's/^~/$HOME/'\``
if test -n "$PIDFILE" -a "$PIDFILE" != "none"; then
	RUNNING=no

	if test -r "$PIDFILE"; then
		if kill -0 `cat $PIDFILE` > /dev/null 2>&1; then
			RUNNING=yes
		else
			echo "PID file, but no dircproxy. :("
		fi
	else
		echo "No PID file"
	fi

	if test "$RUNNING" = "no"; then
		echo "Restarting dircproxy"
		$DIRCPROXY
	fi
else
	echo "Couldn't locate pid_file directive in config file $CONFFILE"
	exit 1
fi
