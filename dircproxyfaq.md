# Introduction #

---


## What is dircproxy? ##

dircproxy is an IRC proxy server designed for people who use IRC from lots of different workstations or clients, but wish to remain connected and see what they missed while they were away. You connect to IRC through dircproxy, and it keeps you connected to the server, even after you detach your client from it.

While you're detached, it logs channel and private messages as well as important events, and when you re-attach it'll let you know what you missed.

This can be used to give you roughly the same functionality as using ircII and screen together, except you can use whatever IRC client you like, including X ones!

## Where can I get dircproxy? ##

Click the Downloads tab at the top of the page.

You may also use SVN to download a copy of the source.  Instructions for downloading a copy are on that page.

## Are there any mailing lists? ##

Yes, there are two user mailing lists and two developer mailing lists.

The user mailing lists consist of a low traffic one which only receives posts concerning new releases and the occasional announcement [dircproxy-announce](http://groups.google.com/group/dircproxy-announce) and a public mailing list for general dircproxy discussion [dircproxy-users](http://groups.google.com/group/dircproxy-users).

For developers there's a mailing list for discussion about dircproxy development [dircproxy-devel](http://groups.google.com/group/dircproxy-devel) and a mailing list where SVN commit logs are sent [dircproxy-svn](http://groups.google.com/group/dircproxy-svn).


## Is there a dircproxy IRC channel? ##

Yes, on the freenode IRC network (irc.freenode.net) called #dircproxy.

## How do I report bugs? ##

Go to the Issues tab and click New Issue.

## Is there an anonymous CVS server? ##

Not any more, the CVS tree was recently imported into a SVN repository. See the Source tab for more.

## How do I add a question to the FAQ? ##

This FAQ is now part of the dircproxy wiki setup so anyone can add/edit entries.  If the entry proves to be incorrect or not appropriate it will be edited.


# Installation #

---


## How do I compile and install dircproxy? ##

Read the INSTALL file in the dircproxy distribution, or see DircproxyInstallation

## I get "couldn't find your xxx() function" from configure ##

Dircproxy makes very few requirements on your system and your libc.  The only things it does require are TCP/IP support through the socket() function, DNS resolver support through the gethostbyname() function and encryption support through the crypt() function.

The 'configure' program checks a few likely locations for these functions and if it can't find them will generate the appropriate warning.  For example:

```
         checking for crypt... no
	 checking for crypt in -lcrypt... no
	 configure: warning: couldn't find your crypt() function
```

If you know which library these functions are located in you  can pass the LDFLAGS shell variable to tell it.  For example, if you know your crypt() function is in the libdes.so (example only!) library, you can do this:

```
         $ LDFLAGS=-ldes ./configure ...
```

Then let me know what type of system you have and what you did, so I can make future versions of dircproxy detect this case automatically.

If you can't find these functions, you'll need to chat to your local sysadmin or UNIX guru and get them to upgrade your libc to something a little more up to date.

## Compilation fails with "xxx.h: No such file or directory" ##

This most likely means that a system header file that dircproxy needs wasn't found on your system.  It can also mean that the dircproxy source isn't complete.

Find out which directory that header file is on your system and pass that to the 'configure' program using the CFLAGS shell variable like this:

```
         $ CFLAGS=-I/path/to/directory ./configure ...
```

If you can't find it, you'll need to find out what your system's equivalent is, then let us know all about it so we can enable future versions of dircproxy to support your system fully.


If there is no equivalent you'll need to get your sysadmin or UNIX guru to upgrade your libc to something more up to date.

## Compilation fails with "undefined reference to `xxx'" ##

This means that a system function dircproxy uses wasn't found on your system.  You'll need to find out in which library on your system that function is.  Then if for example it is in libmisc.so, pass that using the LDFLAGS shall variable to 'configure' like this:

```
         $ LDFLAGS=-lmisc ./configure ...
```

If your system doesn't have that function, you'll need to get your sysadmin or local UNIX guru to upgrade your libc to something a little more up to date.

## What does --enable-debug do? ##

It's primary use is to debug dircproxy but it can also be used by anyone else who wants to help out debugging it.

One main difference is that dircproxy will not switch to the background, but will stay in the foreground and write a lot of strange information (including a record of all text received from the client and server) to the console.  This reverses the meaning of the -D parameter.

It also causes dircproxy to use its built-in versions of strdup() sprintf() and vsprintf() instead of any that might exist in your libc.

Finally it switches on a lot of expensive memory debugging code that records every malloc(), realloc() and free(), notes what C file and line it occurred in and pads the memory with random junk to detect most buffer overruns.  On termination you will see a memory report (hopefully saying "0 bytes in use"), you can also send a USR1 signal to dircproxy to see how much memory it thinks its using, and a USR2 signal to see exactly what is in its memory and where it was allocated.

This slows down dircproxy a lot and makes it inconvenient to use.  However, for people wanting to do dircproxy code work it is invaluable.

## Is dircproxy supported on Windows 2000? ##

No, it is not supported and most likely never will be.

However, we've heard it works just fine (and WinXP also!)


# Usage #

---


## What are configuration classes? ##

A configuration class defines a possible client/server proxied connection.

Basically you define a connection class, setting a password and server to connect to, then when you connect to dircproxy and give your password for the first time, it automatically connects you to a server.  This connection class is then assigned to your proxy session and cannot be used by anyone else until you cause dircproxy to disconnect from the server (see 3.10).

When you reconnect, all you need to do is supply the password again.  Dircproxy then sees that your connection class is already in use and simply attaches you to that.

This means you don't need to specify any "one time passwords" or magic connection or reconnection commands etc.  dircproxy can be used by simply telling your IRC client to supply a "server password" when it connects -- everything else is automatic.

## Can dircproxy be used as an "open proxy"? ##

Yes '''but''' only if each user knows the password.  Dircproxy does NOT support password-less proxy sessions, if you do that you'll just annoy the IRC operators and get yourself banned from the IRC network.

Open IRC proxies are a '''BAD THING''' and lead to abuse of the IRC network.

You may however use dircproxy as a proxy for many users who know the password.  This can be accomplished by running it from inetd (one of the few reasons to do this).

Set it up as described in README.inetd and then set up a single connection class with the appropriate password, host, etc.  By default,  dircproxy will not remain attached to the server when each client quits so they will need to explicitly do a /DIRCPROXY PERSIST to do that.

## Where in the config file do I put my nickname? ##

You don't.  Dircproxy doesn't connect to the IRC server until you connect to it.  This means it can pick up the nickname from the one your IRC client sends.

All you need to tell dircproxy is what server you want to connect to.  The rest of the information such as your nickname, user name, full name, etc are taken from your IRC client when it first connects to it.

## Can dircproxy be run from inetd? ##

> Yes.  See the README.inetd file in the dircproxy distribution.

## Dircproxy won't start and says I need to define connection classes, how do I do this? ##

You need to create a configuration file and the best way is to copy the example found in the conf subdirectory of the dircproxy, or if its just installed on a machine you are using, the /usr/local/share/dircproxy directory. Copy it to your home directory, call it .dircproxyrc and make sure it has no more than 0600 permissions (-rw-------).

Then edit this file, its very well documented.  The configuration classes are defined at the bottom of the file.

## I started dircproxy, but it hasn't connected to any servers yet, what have I done wrong? ##

Nothing, dircproxy won't connect to the server until you connect an IRC client to it.

## Dircproxy will not accept my password, I know its right as it's exactly what I typed into the configuration file. ##

Dircproxy requires that IRC client passwords in the configuration file are encrypted so that anyone managing to read the file off the disk can't get the password and use your dircproxy session.

Encrypt them using your systems crypt(3) function or by using the dircproxy-crypt(1) utility included with the dircproxy source.

## How do I get a running dircproxy to reread its configuration file? ##

Send it a hang-up (HUP) signal.  The process ID can be obtained using the 'ps' command and then signal sent using the 'kill' command.  On BSD-like machines, this can be done like this:

```
	$ ps aux | grep dircproxy
	user      7410  0.0...
	$ kill -HUP 7410
```

Or on a SysV-like machine, like this:

```
	% ps -ef | grep dircproxy
	user      7410  388...
	$ kill -HUP 7410
```

Note that certain configuration options do not take effect until you reconnect to a server or detach from dircproxy.

## How do I detach from dircproxy? ##

Close your IRC client, probably by typing /QUIT.  You don't need tell dircproxy you're detaching, it can guess that by your connection to it closing.

The exception to this is if you're running dircproxy from inetd. If this is the case you'll need to do a /DIRCPROXY PERSIST before you close to tell it that you want to reconnect later. Dircproxy will tell you what port number to reconnect at.

## How can I get dircproxy to disconnect from the server? ##

Using the /DIRCPROXY QUIT command.  You can specify an optional quit message if you like, for example:

```
	> /DIRCPROXY QUIT Right, four weeks in the sun, here I come!
```

## Can I connect multiple clients to the same running session? ##

No.  After all, which one do you listen to?  It could all get very schizophrenic with two people typing under the same nickname from different computers.

## Can one dircproxy connection class be used to connect to multiple servers? ##

No.  It might sound fairly simple to implement at first, tracking channels is fairly easy, its tracking nicknames thats the problem.

## Why doesn't dircproxy ever keep my nickname? ##

First of all, check you **don't** have the following in your configuration file.

```
	nick_keep no
```

"yes" is the default, so if this option isn't set, then it will  be used.

Dircproxy will attempt to keep whatever nickname your client last set using the NICK command.  This means that if you connect a client while dircproxy's attempting to restore your nickname and your client reacts to the server messages (as most "clever" clients do) then dircproxy will accept the new nickname and stop guarding the old one.

If it never restores your nickname when left without a client connected, it may be that the server believes dircproxy is changing it's nickname "too fast" or "too many times".  The default is to attempt to restore the nickname once per minute.  You can adjust this by changing the 'NICK\_GUARD\_TIME' #define in dircproxy.h.

## Can I get dircproxy to automatically connect to the server without connecting at least once? ##

No.  Dircproxy was never written to connect to an IRC server without seeing an IRC client first.

## Does dircproxy support user scripts to extend it? ##


No.  Dircproxy is intended purely as an IRC proxy.  Scripting support would make it too much like a bot, if you want that kind of thing then a bot would serve you much better.

## Does dircproxy provide auto-op or auto-voice support? ##

No.  Again this functionality would be better supplied by a bot.

Chris Crowther has written a patch to provide this kind of support using dircproxy.  This isn't an official patch but it is available from:

```
	http://www.shad0w.org.uk/code/
```


# Logging #

---


## Why does dircproxy by default log channels while I'm attached? ##

Dircproxy was originally designed to give roughly the same functionality as using ircII and screen but allowing you to use X clients (which can't be screen'd).

This means it tries to give you a "full screen" of text when you reattach, so if you've only just disconnected, a full screen includes that which happened before you disconnected.

Its actually quite useful when you think about it:

```
	Argh, I think my computer's about to crash?
	Hi there :)  My name is X.
	-dircproxy- You disconnected
	Do you want to go out for a drink sometime?
	-dircproxy- You connected
	Hi X, sure :)
```

You might not have seen your dream date's name :)  It is also handy for reference.

You can always switch it off though by setting this in the config file:

```
	chan_log_always no
```

## How do I log everything and keep it for my own reference in years to come? ##

Create a directory for dircproxy to store the logs in and then tell dircproxy to store a "permanent copy" in that directory like this:

```
	chan_log_copydir /path/to/directory
	other_log_copydir /path/to/directory
```

## Why does the permanent copy contain text from all the clients dircproxy is proxying? ##

Because you've set the directory name globally for all connection classes in the configuration file.  Also, dircproxy doesn't use the logs itself as its not a security risk or anything to be able to do this and, if each person is on a different channel, its quite a handy thing to do.

If you don't want it to do this, define the 'chan\_log\_copydir' and 'other\_log\_copydir' inside each connection class instead of the global level, like this:

```
	connection {
	:
	chan_log_copydir /path/to/directory
	other_log_copydir /path/to/directory
	:
	}

	connection {
	:
	chan_log_copydir /path/to/directory2
	other_log_copydir /path/to/directory2
	:
	}
```

## How do I log private messages to individual files? ##

Use the 'privmsg-log.pl' script in the contrib directory of the dircproxy source as a filter as an 'other\_log\_program'.  You will need to edit the script to set the directory to store these private message log files and need to add the following to your dircproxy configuration file:

```
	other_log_program "/usr/local/share/dircproxy/privmsg-log.pl"
```

## How do I log outgoing private messages? ##

You can't.  There is no way for dircproxy to replay them for you (every client tested has ignored them).


# DCC Proxying #

---


## Why would I want dircproxy to proxy DCC requests as well? ##

DCC proxying means that all DCC requests get proxied through dircproxy just like your normal IRC requests.  This means your real IP address is just as hidden as it is on IRC. Also, if you're running dircproxy on your NAT firewall so you can actually get on IRC, you'll be able to do DCCs to as it will be proxying them between the two networks for you.

## What should the "Local IP" setting on my IRC client be set to? ##

It must be set to the local IP address of the machine running that IRC client.  Specifically this is the address dircproxy will connect to your client on while performing DCC proxying.

Do '''not''' set it to the IP address of the machine running dircproxy.  Dircproxy will perform that change for you when doing DCC proxying.

If your IRC client machine has two or more interfaces, set it to the address that dircproxy will be able to connect to it on.

Please note that if your IRC client has a "Detect from Server" setting, this may not work as intended in dircproxy.  It is better to set it manually.

## What's "DCC over ssh" and how do I use it? ##

For a complete description, including how to do it, see the README.dcc-via-ssh file in the dircproxy distribution.

Simply its a way of doing DCCs over ssh tunnels to get around any firewalls that might be in the way that would normally prevent even the most determined proxy from allowing DCCs.

## Does text I type before dircproxy tells me the remote peer has connected ever reach them? ##

Yes. It is simply queued until the remote side connects, then is sent to them.

## Why doesn't dircproxy support DCC RESUME? ##

The DCC RESUME protocol is an mIRC extension to the standard and is documented at  http://www.mirc.co.uk/help/dccresum.txt

Dircproxy does not support this protocol, therefore you cannot resume DCC transfers if you are using dircproxy.  DCC RESUME support will **never** be implemented in dircproxy, the reason for this is as follows.

Ordinary DCC SENDs are offered by the sender sending a DCC SEND message to the receiver containing a ip address and port number.  The receiver accepts the offer by connecting to it.

```
	[ sender ] --(1) DCC SEND--> [ receiver ]
	<---(2) accept---
```

When proxied through dircproxy, dircproxy accepts the offer itself, then makes a new offer to the receiver.  This means that dircproxy has already usually received a fair portion of the file before the receiver accepts dircproxy's own offer. This greatly increases the speed of transfers.

```
	[ sender ] --(1) DCC SEND--> [ proxy ] --(3) DCC SEND--> [ receiver ]
	<---(2) accept---
	<---(4) accept---
```

If the DCC RESUME protocol is used, the receiver indicates it wishes to do so by sending a DCC RESUME message to the sender instead of connecting to it, the connect is only done after a DCC ACCEPT is received from the sender.

```
	[ sender ] --(1) DCC SEND--> [ receiver ]
	<-(2) DCC RESUME-
	-(3) DCC ACCEPT->
	<---(4) accept---
```

Of course, with dircproxy proxying, this is too late, it's already accepted the sender's offer and is receiving the file from the beginning.

Ryan Tolboom has written a patch to enable DCC RESUME for files that dircproxy has captured, this is available from http://www-ec.njit.edu/~rxt1077/dircproxy-resume/


# Advanced Usage #

---


## How do I change what username is presented on IRC? ##

The obvious answer is to run dircproxy under that username, but that doesn't help if you're proxying for multiple people. Another option is to use one of the many fake ident daemons to return a false answer for you.

There's also a third option, which is available to those running dircproxy as root (either as a daemon or from inetd).  You can use the 'switch\_user' configuration file directive. This ensures that the connection to the server appears as from whatever local username you give it (by seteuid()ing to that briefly) while the dircproxy process remains as root.