## Process this file with automake to produce Makefile.in

INCLUDES = \
	-lssl -lcrypto \
	-DSYSCONFDIR="\"${sysconfdir}\""

bin_PROGRAMS = \
	dircproxy

dircproxy_SOURCES = \
	main.c \
	dircproxy.h \
	irc_net.c irc_net.h \
	irc_client.c irc_client.h logo.h help.h \
	irc_server.c irc_server.h \
	irc_prot.c irc_prot.h \
	irc_log.c irc_log.h \
	irc_string.c irc_string.h \
	dcc_net.c dcc_net.h \
	dcc_chat.c dcc_chat.h \
	dcc_send.c dcc_send.h \
	cfgfile.c cfgfile.h \
	timers.c timers.h \
	dns.c dns.h \
	net.c net.h \
	match.c match.h \
	stringex.c stringex.h \
	sprintf.c sprintf.h \
	memdebug.c memdebug.h

dircproxy_LDADD = \
	../getopt/libgetopt.a
