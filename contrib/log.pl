#!/usr/bin/perl
# Perl script to take dircproxy log information and e-mail it to
# an address if it contains certain words or is from certain people.
#
# To use, set the following in dircproxyrc:
#   chan_log_program "/path/to/log.pl"
#  or
#   other_log_program "/path/to/log.pl"
#

use vars qw/$mailto @match @from $sendmail/;
use strict;

# Address to e-mail to
$mailto = 'nobody@localhost';

# Words to match on
@match = ('rabbit', 'llama');

# People to always send
@from = ('Joe', 'Bloggs');

# Path to sendmail
$sendmail = '/usr/lib/sendmail';


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

my $notice = ($source =~ /^-/ ? 1 : 0);
$source =~ s/^.//;
$source =~ s/.$//;

my ($nickname, $username, $hostname);
my $server = 0;
if ($source =~ /^([^!]*)!([^\@]*)\@(.*)$/) {
	($nickname, $username, $hostname) = ($1, $2, $3);
} else {
	$nickname = $source;
	$server = 1 if $notice;
}


#------------------------------------------------------------------------------#

# The second argument to this script is who it was to (your nickname or
# a channel name)

my $dest = shift(@ARGV);


#------------------------------------------------------------------------------#

# The text to log is on the standard input.

my $text = <STDIN>;
die "No text given by dircproxy" unless $text && length $text;


#------------------------------------------------------------------------------#

my $mailit = 0;

# Always mail server messages (including those from dircproxy)
$mailit = 1 if $server;

# Check the from
foreach my $from (@from) {
	$mailit = 1 if lc($nickname) eq lc($from);
}

# Check the text
foreach my $match (@match) {
	$mailit = 1 if $text =~ /$match/i;
}

#------------------------------------------------------------------------------#

if ($mailit) {
	my $subject = "";
	if ($server) {
		$subject .= "Server message";
	} elsif ($notice) {
		$subject .= "Notice";
	} else {
		$subject .= "Message";
	}
	$subject .= " from " . $nickname;
	$subject .= " ($username\@$hostname)" unless $server;
	
	open MAILER, '|' . $sendmail . ' -t';
	print MAILER "From: dircproxy\n";
	print MAILER "To: $mailto\n";
	print MAILER "Subject: $subject\n";
	print MAILER "\n";
	print MAILER "Sent to $dest\n" if $dest;
	print MAILER $text;
	close MAILER;
}
