#!/usr/bin/perl
# Logs private messages to seperate files.
#
# To use, set the following in dircproxyrc:
#   other_log_program "/path/to/privmsg-log.pl"
#

use vars qw/$logdir/;
use strict;

# Directory to store files in
$logdir = '/tmp';


#------------------------------------------------------------------------------#

# The first argument to this script is the source of the text.  Its in the
# following formats
#
# -dircproxy-
#     Notice from dircproxy
#
# -servername-
#     Notice from a server
#
# <nick!username@host>
#     Private message from a person
#
# -nick!username@host-
#     Notice from a person
#
# [nick!username@host]
#     Unfiltered CTCP message (usually an ACTION) from a person
#

my $source = shift(@ARGV);
die "No source given by dircproxy" unless $source && length $source;

my $nickname;
if ($source =~ /^[<-\[]([^!]*)![^\@]*\@.*[>-\]]$/) {
	$nickname = $1;
} else {
	# Not a private message, don't want it
	exit 0;
}


#------------------------------------------------------------------------------#

# The second argument to this script is who it was to (your nickname or
# a channel name)

my $dest = shift(@ARGV);
if ($dest =~ /^#/) {
	# A channel message, don't want it
	exit 0;
}


#------------------------------------------------------------------------------#

# The text to log is on the standard input.

my $text = <STDIN>;
die "No text given by dircproxy" unless $text && length $text;


#------------------------------------------------------------------------------#

open LOGFILE, ">>$logdir/$nickname";
print LOGFILE $source . " " . $text;
close LOGFILE;
