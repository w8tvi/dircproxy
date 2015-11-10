# How to Configure Server or proxy #

## Using the TOR Anonymous Network ##

if you want to use tor to connect to a regular IRC server
```
./torify dircproxy
```

If you want to connect to a hidden irc server, it's a little tricky as dircproxy doesn't support sock4 natively.

```
 $ dig  +short  irc.tor.freenode.net  cname
   mejokbp2brhw4omd.onion.
 $ socat TCP4-LISTEN:57001 SOCKS4A:127.0.0.1:mejokbp2brhw4omd.onion:6667,socksport=9050
```

in your connexion class use the server `localhost:57001` and it should work.

## Bitlbee ##

You can use dircproxy with bitlbee without problem and having all the feature of dircproxy with your IM conversation.