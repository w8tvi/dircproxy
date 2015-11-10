# Howto configure your irc client #


## XChat: ##

To connect to dircproxy with an XChat Client, add the following to your "Chatline":

```
/server [servername] [dircproxyport] [password]
```
for example:

```
/server securiweb.net 57000 12345
```


Have fun
KriS

## mIRC: ##

To connect to dircproxy with an mIRC Client, type the following in your "Chatline":

```
/server -m [dircproxyhost]:[dircproxyport] [password]
```
for example:

```
/server -m 192.168.1.2:57000 12345
```

## irssi: ##

To connect to dircproxy with an irssi Client, enter the following to your .irssi/config:

```
settings = {
  core = {
    ....
    use_proxy = "yes";
    proxy_address = "xxx.xxx.xxx.xxx";
    proxy_port = "7337";
    proxy_password = "";
    proxy_string = "";
  };
};
```


## Chatzilla ##

To connect to dircproxy with a Chatzilla Client simply enter the following in your mozilla browser window url entry bar:

```
irc://server:port/,needpass
```

server = dircproxy

port   = dircproxy port

/,needpass = type exactly as shown

## Gaim ##
To connect to dircproxy with Gaim, set the following settings in your account:

Server: dircproxy server

Password: dircproxy password

(check "Remember password")

Expand "Show More Options"

Port: dircproxy port

Leave "Proxy Type" unchanged

## Trillian ##
To connect to dircproxy using Trillian:
  * In Trillian's main window, click the IRC button (grey ball)
  * Click 'Manage my connections...'
  * Click 'Add a new connection...'
  * Click 'IRC'
  * In the server field (second field, marked ''irc.trillian.com::6667''), enter ''proxy.host.name:password:port'', substituting the correct values
  * Fill in the other fields with the names you choose
  * Click 'Connect'