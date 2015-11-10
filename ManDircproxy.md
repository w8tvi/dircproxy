# NAME #
**dircproxy** - Detachable Internal Relay Chat Proxy Server

# SYNOPSIS #
**dircproxy** [**-hvDI**] [**-f** _config\_file_] [**-P** _listen\_port_] [**-p** _pid\_file_]

# DESCRIPTION #
**dircproxy** is an IRC proxy server designed for people who use IRC from lots of different workstations or clients, but wish to remain connected and see what they missed while they were away.
> You connect to IRC through **dircproxy**, and it keeps you connected to the server, even after you detach your client from it. While you're detached, it logs channel and private messages as well as important events, and when you re-attach it'll let you know what you missed.
> This can be used to give you roughly the same functionality as using ircII and **screen**(**8**) together, except you can use whatever IRC client you like, including X ones!
> Authentication is provided by a password, and optional hostname checking. This links it to a connection class specified in the configuration file. Only one user may use a connection class at one time, when that user detaches, the connection to the server is kept open. When someone (usually the user) subsequently connects to **dircproxy** and provides the same password, they are reconnected to the connection to the server, instead of having a new connection created for them.
> Multiple connection classes can be defined, allowing multiple people to use the same proxy.
**dircproxy** can use either a _.dircproxyrc_ file in the user's home directory, or a system-wide dircproxyrc file. It will load the first it finds (home directory first, then system-wide). If no configuration file is specified, it will not start.

# OPTIONS #
**-f** _config\_file_
> Specifies the configuration file to be used, overriding the default search list.
**-h**
> Displays a brief help message detailing the command-line arguments, then exits.
**-v**
> Displays the **dircproxy** version number, then exits.
**-D**
> Run in the foreground and do not fork into the background.
**-I**
> Use to indicate **dircproxy** is being run from the **inetd**(**8**) daemon. This implies **-D**. For more information on running **dircproxy** under **inetd**(**8**), see the _README_.inetd file.
**-P** _listen\_port_
> Specifies an alternate port to use, overriding the default and any value specified in the configuration file. You can also add the IP-address of the adapter you want dircproxy to bind to, e.g.: 192.168.64.1:7007
**-p** _pid\_file_
> Specifies a file to write the process id to, overriding the default and any value specified in the configuration file.

# CONFIGURATION #
> The configuration file has the following format:
> Empty lines and lines starting with '#' are comments.
> Connection classes start with 'connection {' and end with '}'. They obtain default values from all the entries above them in the configuration file, and may contain values of their own.
> Otherwise a line is of the format 'keywords arguments'. If the argument contains spaces it should be contained in double quotes ('"with spaces"'). The possible keywords and their meanings are as follows (note that the configuration file is not case-sensitive):
# GLOBAL OPTIONS #
> These options may not be placed inside a connection class as they affect the operation of the entire **dircproxy** server.
**listen\_port**
> What port should **dircproxy** listen for connections from IRC clients on?
> This can be a numeric port number, or a service name from /etc/services. You can also enter the IP-address of the adapter you want **dircproxy** to bind to, e.g.: 192.168.64.1:7007
**pid\_file**
> File to write the dircproxy process id to on startup. If you start this with a "~/" then it refers to a file in a directory under your home directory.

> none = Don't write pid file
**client\_timeout**
> Maxmimum amount of time (in seconds) a client can take to connect to dircproxy and provide their password and nickname etc.
**connect\_timeout**
> Maximum amount of time (in seconds) a client has to provide a server to connect to after they've logged in. This only applies if 'server\_autoconnect' is 'no' for that class.
**dns\_timeout**
> Maximum amount of time (in seconds) to wait for a reply from a DNS server. If the time exceeds this then the lookup is cancelled.
# LOCAL OPTIONS #
> These options may be placed in a connection class, or outside of one. If they are outside then they only affect those connection classes defined afterwards.
# SERVER OPTIONS #
> Options affecting the connection to the IRC server.
**server\_port**
> What port do we connect to IRC servers on if the server string doesn't explicitly set one
> This can be a numeric port number, or a service name from /etc/services
**server\_retry**
> How many seconds after disconnection or last connection attempt do we wait before retrying again?
**server\_maxattempts**
> If we are disconnected from the server, how many times should we iterate the server list before giving up and declaring the proxied connection dead?
> 0 = iterate forever
**server\_maxinitattempts**
> On first connection, how many times should we iterate the server list before giving up and declaring the proxied connection dead?

> 0 = iterate forever.  This isn't recommended.
**server\_keepalive**
> This checks whether the dircproxy to server connection is alive at the TCP level. If no data is sent in either direction for a period of time, a TCP keepalive probe is sent.

> yes = send keepalive probes
> no = don't send keepalive probes
**server\_pingtimeout**
> For some people, dircproxy doesn't notice that the connection to the server has been dropped because the socket remains open. For example, those behind a NAT'd firewall. dircproxy can ping the server and make sure it gets replies back. If the time since the last reply was received exceeds the number of seconds below the server is assumed to be "stoned" and dircproxy leaves it. If you have a high latency connection to the server, it can wrongly assume the server is stoned because the PINGs don't arrive in time. Either raise the value, or use the 'server\_keepalive' option instead.

> 0 = don't send PINGs
**server\_throttle**
> To prevent you from being flooded off the IRC network, dircproxy can throttle the connection to the server to prevent too much being sent within a certain time period.
> For this you specify a number of bytes, then optionally a time period in seconds seperated by a colon. If the time period is ommitted then per second is assmued.

> server\_throttle 10        # 10 bytes per second
> server\_throttle 10:2      # 10 bytes per 2 seconds (5 per second)

> 0 = do not throttle the connection
**server\_autoconnect**
> Should dircproxy automatically connect to the first server in the list when you connect. If you set this to 'no', then 'allow\_jump' is automatically set to 'yes'. If 'allow\_jump\_new' is also 'yes', then you can create connection classes with no 'server' lines.

> yes = Automatically connect to the first server
> no = Wait for a /DIRCPROXY JUMP from the client
# CHANNEL OPTIONS #
> Options affecting channels you join.
**channel\_rejoin**
> If we are kicked off a channel, how many seconds do we wait before attempting to rejoin.

> -1 = Don't rejoin
> 0 = Immediately
**channel\_leave\_on\_detach**
> Should dircproxy automatically make you leave all the channels you were on when you detach?

> yes = Leave them
> no = Remain on them
**channel\_rejoin\_on\_attach**
> If 'channel\_leave\_on\_detach' is 'yes' then should dircproxy rejoin those channels when you attach again?

> yes = Rejoin the channels dircproxy automatically left
> no = Leave permanently on detach
# IDLE OPTIONS #
> Options affecting idle times on IRC.
**idle\_maxtime**
> Set this to the maximum amount of time you want to appear idle for while on IRC, if you set this then dircproxy will reset your idle time if it reaches this limit (in seconds).

> 0 = Don't reset idle time
# DISCONNECTiON OPTIONS #
> Options affecting when dircproxy disconnects you.
disconnect\_existing\_user
> If, when you connect to dircproxy, another client is already using your connection class (ie, if you forgot to close that one), then this option lets you automatically kill that one off. Make sure you turn any "automatic reconnect to server" options off before using this, otherwise you'll have a fight on your hands.

> yes = Yes, disconnect
> no = No, don't let me on
**disconnect\_on\_detach**
> When you detach from dircproxy it usually keeps you connected to the server until you connect again. If you don't want this, and you want it to close your server connection as well, then set this.

> yes = Close session on disconnection
> no = Stay connected to server until reattachment
# MODE OPTIONS #
> Options affecting user modes set by the IRC server.
**initial\_modes**
> Which user modes should we automatically set when you first connect to a server. Just in case you forget to do it yourself with your irc client.
> Set to "" to not set any modes.
**drop\_modes**
> Which user modes to drop automatically when you detach, handy to limit the impact that your client has while connected, or for extra security if you're an IRCop.
> Set to "" to not drop any modes.
**refuse\_modes**
> Which user modes to refuse to accept from a server. If the server attempts to set one of these, then the connection to it will be dropped and the next server in the list will be tried.
> A good setting for many people would be "+r", as most servers use that to mean your connection is restricted. Don't set it to this if you're on DALnet however, DALnet uses +r to indicate you have registered with NickServ (gee, thanks guys!).
> Set to "" to not refuse any modes.
# ADDRESS OPTIONS #
> Options affecting your address on IRC.
**local\_address**
> Local hostname to use when connecting to an IRC server. This provides the same functionality as the ircII -H parameter.

> none = Do not bind any specific hostname
# MESSAGE OPTIONS #
> Options affecting messages sent or set by dircproxy on behalf of you.
**away\_message**
> If you don't explicitly set an /AWAY message before you detach, dircproxy can for you, so people don't think you are really at your keyboard when you're not.

> none = Do not set an away message for you
**quit\_message**
> If you don't explicitly give a message when you /DIRCPROXY QUIT, this will be used instead. Also used for when you've sent dircproxy not to remain attached to the server on detachment.

> none = Use dircproxy version number as QUIT message
**attach\_message**
dircproxy can send an announcement onto every channel you are on when you reattach to it, just to let everyone know you are back. If you start this with "/ME " then it will be sent as an ACTION CTCP message (just like the ircII /me command).

> none = Do not announce attachment
**detach\_message**
dircproxy can send an announcement onto every channel you are on when you detach from it, just to let everyone know you are gone. If you start this with "/ME " then it will be sent as an ACTION CTCP message (just like the ircII /me command).

> none = Do not announce detachment
**detach\_nickname**
> Nickname to change to automatically after you detach, to indicate you are away for example. If this contains a '**' character, then that character is replaced with whataver your nickname was before you detached (ie "**_away" adds "_away" to the end of your nickname);

> none = Leave nickname as it is
# NICKNAME OPTIONS #
> Options affecting your nickname
**nick\_keep**
> Whether dircproxy should attempt to keep the nickname you last set using your client. If this is 'yes' and your nickname is lost while your client is disconnected, then it will keep on trying to get it back until a client connects again.

> yes = try to keep my nickname while I'm disconnected
> no = if it changes, leave it
# CTCP OPTIONS #
> Options affecting CTCP replies
**ctcp\_replies**
> Whether dircproxy should reply to the standard set of CTCP messages while the client is detached.

> yes = reply to ctcp messages while client is detached
> no = nothing but silence
# LOGGING OPTIONS #
> These options affect both the internal logging inside dircproxy so messages can be recalled to you when you return from being disconnected, and general logging for your own personal use.
**log\_timestamp**
> Log messages can have a timestamp added to the front to let you know exactly when a message was logged. The format of this timestamp depends on the setting of 'log\_relativetime'.

> yes = Include a timestamp in all log messages
> no = Do not include a timestamp
**log\_relativetime**
> If 'log\_timestamp' is 'yes' then you have the option of using either intelligent relative timestamps, or ordinary fixed timestamps. If you choose relative, then the timestamp shown when log information is recalled to your client depends on how old that line is, with possible date information if it is a really old message. If you do not choose relative then only the time (in HH:MM format) will be logged.
> This obviously has no effect on the log files under the directory specified by 'log\_dir'.

> yes = Use relative timestamps
> no = Use fixed timestamps
**log\_timeoffset**
> Difference in minutes from your IRC client to the dircproxy machine. So if you're in GMT, but your dircproxy machine is in PST (which is 8 hours behind), then this would be -(8 **60) = -480. Used to adjust log file timestamps so they're in the right time zone for you.**

> 0 = Don't adjust log timestamps.
**log\_events**
> Events you want dircproxy to log for you. This is a comma seperated list of event names, prefixed with '+' to add the event to the list or '-' to remove an event. You can also specify 'all' to log all events (the default) or 'none' to not log anything.
> Example, to just log text and action's:

> log\_events "none,+text,+action"
> Example, to log everything but server messages:

> log\_events "all,-server"
  1. you don't need to specify 'all'
> log\_events -server
> The possible events are:
**text**
> Channel text and private messages
**action**
> CTCP ACTION events (/me) sent to you or channels
**ctcp**
> Whether to record whether a CTCP was sent to you
**join**
> People (including you) joining channels
**part**
> People (including you) leaving channels
**kick**
> People (including you) being kicked from channels
**quit**
> People quit'ing from IRC
**nick**
> People (including you) changing nickname
**mode**
> Changes in channel modes or your own personal mode
**topic**
> Changes to the channel topic
**client**
> You detaching and attaching
**server**
> Connections and disconnections from servers
**error**
> Problems and errors dircproxy encounters (recommended!)
**log\_dir**
dircproxy keeps it's own internal log files (under /tmp) so it can recall information to your client when you reconnect. It can also log messages to files for your own use.
> Under this directory a file will be created named after each channel you join, a file will be created named after each nickname that sends you private messages, or you send, and a final file called "server" will be created containing server events.
> This logging is done regardless of the enabled or always settings, which only affect the internal logging. However the log\_events settings do affect what is logged.
> If you start with "~/" then it will use a directory under your home directory.

> none = Do not create log files for your own use
**log\_program**
> Program to pipe log messages into. If given, dircproxy will run this program for each log message giving the full source information as the first argument, the destination as the second and the message itself as a single line on standard input.
> The program can be anywhere in your $PATH, or you can start it with "~/" if its in a directory under your home directory.
> This logging is done regardless of the enabled or always settings, which only affect the internal logging. However the log\_events settings do affect what is logged.

> none = Do not pipe log messages to a program
# INTERNAL CHANNEL LOG OPTIONS #
> Options affecting the internal logging of channel text so it can be recalled to your client when you reconnect. These options only apply if the 'chan\_log\_enabled' option is set to 'yes'.
**chan\_log\_enabled**
> Whether logging of channel text for later recall, so you can see what you missed, should take place.

> yes = Channel text is logged for recall
> no = Channel text is NOT logged for recall
**chan\_log\_always**
> Channel text will always be logged for later recall while you are offline, so when you come back you can see what you missed. You can also, if you wish, log channel text while you are online, so if you're only away a short time you can get an idea of any context.

> yes = Log channel text for recall while offline and online
> no = Log channel text for recall only while offline
**chan\_log\_maxsize**
> To preserve your harddisk space, you can limit the size of the internal channel log file, which is stored in the /tmp directory. Once the log file reaches this number of lines, every line added will result in a line being removed from the top. If you know you are never going to want all that logged information, this might be a good setting for you.

> 0 = No limit to internal log file size
**chan\_log\_recall**
> Number of lines from the bottom of each internal channel log to automatically recall to your IRC client when you reconnect. If this is low, you may not get much useful information, if this is high, it may take a long time for all the information to arrive.

> -1 = Recall the whole log (not recommended if chan\_log\_always is yes)
> 0 = Don't automatically recall anything
# INTERNAL PRIVATE LOG OPTIONS #
> Options affecting the internal logging of private messages, notices, CTCP and DCC events so they can be recalled to your client when you reconnect. These options only apply if the 'private\_log\_enabled' option is set to 'yes'.
**private\_log\_enabled**
> Whether logging of private messages for later recall, so you can see what you missed, should take place.

> yes = Private messages are logged for recall
> no = Private messages are NOT logged for recall
**private\_log\_always**
> Private messages will always be logged for later recall while you are offline, so when you come back you can see what you missed. You can also, if you wish, log private messages while you are online, so if you're only away a short time you can get an idea of any context.

> yes = Log private messages for recall while offline and online
> no = Log private messages for recall only while offline
**private\_log\_maxsize**
> To preserve your harddisk space, you can limit the size of the internal private message log file, which is stored in the /tmp directory. Once the log file reaches this number of lines, every line added will result in a line being removed from the top. If you know you are never going to want all that logged information, this might be a good setting for you.

> 0 = No limit to internal log file size
**private\_log\_recall**
> Number of lines from the bottom of the internal private message log to automatically recall to your IRC client when you reconnect. If this is low, you may not get much useful information, if this is high, it may take a long time for all the information to arrive.

> -1 = Recall the whole log (not recommended if private\_log\_always is yes)
> 0 = Don't automatically recall anything
# INTERNAL SERVER LOG OPTIONS #
> Options affecting the internal logging of server messages so they can be recalled to your client when you reconnect. These options only apply if the 'server\_log\_enabled' option is set to 'yes'.
**server\_log\_enabled**
> Whether logging of server messages for later recall, so you can see what you missed, should take place.

> yes = Server messages are logged for recall
> no = Server messages are NOT logged for recall
**server\_log\_always**
> Server messages will always be logged for later recall while you are offline, so when you come back you can see what you missed. You can also, if you wish, log server messages while you are online, so if you're only away a short time you can get an idea of any context.

> yes = Log server messages for recall while offline and online
> no = Log server messages for recall only while offline
**server\_log\_maxsize**
> To preserve your harddisk space, you can limit the size of the internal server message log file, which is stored in the /tmp directory. Once the log file reaches this number of lines, every line added will result in a line being removed from the top. If you know you are never going to want all that logged information, this might be a good setting for you.

> 0 = No limit to internal log file size
**server\_log\_recall**
> Number of lines from the bottom of the internal server message log to automatically recall to your IRC client when you reconnect. If this is low, you may not get much useful information, if this is high, it may take a long time for all the information to arrive.

> -1 = Recall the whole log (not recommended if server\_log\_always is yes)
> 0 = Don't automatically recall anything
# DCC PROXY OPTIONS #
> Options affecting proxying and capturing of DCC chat and send requests.
**dcc\_proxy\_incoming**
> Whether dircproxy should proxy DCC chat and send requests sent to you by others on IRC.

> yes = Proxy incoming requests.
> no = Do not proxy incoming requests.
**dcc\_proxy\_outgoing**
> Whether dircproxy should proxy DCC chat and send requests sent by you to others on IRC.

> yes = Proxy outgoing requests.
> no = Do not proxy outgoing requests.
**dcc\_proxy\_ports**
> Ports that dircproxy can use to listen for DCC connections on. This is for when you're behind a firewall that only allows certain ports through, or when doing DCC-via-ssh.
> It is a comma seperated list of port numbers or ranges of ports, for example '57100-57199,57400,57500,57600-57800'

> any = Use any port given to us by the kernel.
**dcc\_proxy\_timeout**
> Maxmimum amount of time (in seconds) to allow for both sides of a DCC proxy to be connected.
**dcc\_proxy\_sendreject**
> Whether to send a physical REJECT message via CTCP back to the source of the request in event of failure.

> yes = Send reject CTCP message back.
> no = Do not send any message back.
**dcc\_send\_fast**
> Whether to ignore the "acknowledgment" packets from the client and just send the file to them as fast as possible. There should be no real danger in doing this.

> yes = Send as fast as possible.
> no = Wait for each packet to be acknowledged.
**dcc\_capture\_directory**
dircproxy can capture files sent via DCC and store them on the server. Especially useful while you are detached, whether it does it while attached or not depends on 'dcc\_capture\_always'. This is the directory to store those captured files in.
> If start with "~/" then it will use a directory under your home directory.

> none = Do not capture files.
**dcc\_capture\_always**
> If we're capturing DCC send's, should we do it while the client is connected as well? If 'yes', then the client will never see the file, it'll be just stored on the server with a notice sent to the client telling them where.

> yes = Capture even when a client is connected.
> no = Capture only when client detached.
**dcc\_capture\_withnick**
> Whether to start the filename of the captured file with the nickname of the sender, so you know who it came from.

> yes = Start with nickname.
> no = Do not alter the filename.
**dcc\_capture\_maxsize**
> Maximum size (in kilobytes) that a captured file can be. If a captured file is larger than this, or becomes larger than this, then the capture will be aborted and the file removed from the disk. Prevents people from filling your disk up while you're detached with a massive file.

> 0 = No limit to file size.
**dcc\_tunnel\_incoming**
> Port of a local ssh tunnel leading to another dircproxy client that we should use for incoming DCC requests. This should not be set if 'dcc\_tunnel\_outgoing' is set.
> See the README.dcc-via-ssh file included with the dircproxy distribution for more information.
> This can be a numeric port number, or a service name from /etc/services

> none = There is no tunnel.
**dcc\_tunnel\_outgoing**
> Port of a local ssh tunnel leading to another dircproxy client that we should use for outgoing DCC requests. This should not be set if 'dcc\_tunnel\_incoming' is set.
> See the README.dcc-via-ssh file included with the dircproxy distribution for more information.
> This can be a numeric port number, or a service name from /etc/services

> none = There is no tunnel.
# ADVANCED OPTIONS #
> Options for the advanced user.
**switch\_user**
> If you're running dircproxy as root, it can switch to a different "effective user id" to create the server connection. This means that your system ident daemon (and therefore IRC, if it queries it) will see your server connection as the user you put here, instead of root.
> This is most useful if you are sysadmin running a dircproxy server for multiple people and want them to all appear as different usernames without using a hacked identd. Because dircproxy is still running as root, it will have those privileges for all operations, including the bind(2) for the 'local\_address' config option if you're using Secure Linux patches.
> This can only be used if your system supports seteuid(2) and if you are running dircproxy as the root user, and not just setuid. Attempting otherwise will generate a warning as dircproxy starts.
> This can be a numeric uid or a username from /etc/passwd.

> none = Do not do this.
# MOTD OPTIONS #
> Options affecting the dircproxy message of the day.
**motd\_logo**
> If this is yes, then the dircproxy logo and version number will be included in the message of the day when you connect. Only the picky would turn this off, its pretty!

> yes = Show me the pretty logo
> no = I don't like logos, I'm boring, I eat llamas.
**motd\_file**
> Custom message of the day file to send when users connect to dircproxy. The contents of this file will be sent after the logo and before the stats. If you start this with a "~/" then it refers to a file in a directory under your home directory.

> none = No custom motd
**motd\_stats**
> Display information on what channels you were on, and log file sizes etc in the message of the day. This is handy, and lets you know how not only much information you missed, but how much will be sent to you.

> yes = Show the stats
> no = They don't interest me, don't show them.
# COMMAND OPTIONS #
> Options allowing or disallowing the use of /DIRCPROXY commands.
**allow\_persist**
> You can disable the /DIRCPROXY PERSIST command if you do not want people using your proxy to be able to do that.

> yes = Command enabled
> no = Command disabled
**allow\_jump**
> You can disable the /DIRCPROXY JUMP command if you do not want people to do that.

> yes = Command enabled
> no = Command disabled
**allow\_jump\_new**
> If the /DIRCPROXY JUMP commmand is enabled, then you can disable it being used to jump to a server:port not in the list specified in the configuration file.

> yes = Can jump to any server
> no = Only ones in the config file
**allow\_host**
> You can disable the /DIRCPROXY HOST command if you do not want people to do that.

> yes = Command enabled
> no = Command disabled
**allow\_die**
> You can enable the /DIRCPROXY DIE command if you want people to be able to kill your proxy. This isn't recommended as a global option, instead only enable it for a specific connection class (ie yours).

> yes = Command enabled
> no = Command disabled
**allow\_users**
> You can enable the /DIRCPROXY USERS command if you want people to be able to see who's using your proxy. This isn't recommended as a global option, instead only enable it for a specific connection class (ie yours).

> yes = Command enabled
> no = Command disabled
**allow\_kill**
> You can enable the /DIRCPROXY KILL command if you want people to be able to disconnect anyone using your proxy (including you!). This isn't recommended as a global option, instead only enable it for a specific connection class (ie yours).

> yes = Command enabled
> no = Command disabled
> Additionally, the following keywords may go only inside a connection class definition. One 'password' and at least one 'server' (unless 'server\_autoconnect' is 'no' and 'allow\_jump\_new' is 'yes') are mandatory.
**password**
> Password required to use this connection class. This should be encrypted using your system's crypt(3) function. It must be the same as the password supplied by the IRC client on connection for this connection class to be used.
> You can use the included dircproxy-crypt(1) utility to generate these passwords.
**server**
> Server to connect to. Multiple servers can be given, in which case they are iterated when the connection to one is dropped. This has the following format:
> [hostname[:[port](port.md)[:password]]
**from**
> The connection hostname must match this mask, multiple masks can be specified to allow more hosts to connect. The **and ? wildcards may be used.**join **> Channels to join when you first connect. Multiple channels can be given, either by seperating the names with a comma, or by specifying multiple  join' lines. You may also include the channel key by seperating it from the channel name with a space.
> Note: You must surround the list of channels with quotes to distinguish from comments.
> For clarification, this is the format of this line:
> join "channel[key](.md)[,channel[key](.md)]..."**

# SIGNALS #
dircproxy will reread its configuration file whenever it receives the hangup signal, SIGHUP.
> Sending an interrupt signal, SIGINT, or a terminate signal, SIGTERM, will cause dircproxy to exit cleanly.

# NOTES #
> More information, including announcements of new releases, can be found at:
http://dircproxy.securiweb.net/

# BUGS #
> Please submit and review bug reports at:
http://dircproxy.securiweb.net/newticket

# AUTHOR #
> Written by Scott James Remnant <scott@netsplit.com>. Additions by Mike Taylor <bear@code-bear.com>.

# COPYRIGHT #
> Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>. Copyright (C) 2004 Francois Harvey <fharvey@securiweb.net> and Mike Taylor <bear@code-bear.com>. dircproxy is distributed under the GNU General Public License.