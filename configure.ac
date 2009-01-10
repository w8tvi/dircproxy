# Process this file with autoconf to produce a configure script.
# $Id: configure.ac,v 1.9 2003/12/31 23:16:25 fharvey Exp $

AC_INIT([dircproxy], [1.2.0], [http://dircproxy.googlecode.com/])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE
AC_COPYRIGHT([Copyright (C) 2009 Noel Shrum & Francois Harvey.])
AC_CONFIG_SRCDIR([src/dircproxy.h])
AM_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_AWK
AC_PROG_RANLIB
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_FUNC([socket],,
	      [AC_CHECK_LIB([socket], [socket],,
	      		    [AC_MSG_WARN([couldn't find your socket() function])])])
AC_CHECK_FUNC([gethostbyname],,
	      [AC_CHECK_LIB([nsl], [gethostbyname],,
	      		    [AC_MSG_WARN([couldn't find your gethostbyname() function])])])
AC_CHECK_FUNC([crypt],,
	      [AC_CHECK_LIB([crypt], [crypt],,
	      		    [AC_MSG_WARN([couldn't find your crypt() function])])])

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h sys/param.h sys/socket.h sys/time.h \
		  crypt.h fcntl.h inttypes.h netdb.h stdlib.h string.h \
		  syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_TYPE_UID_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_MALLOC
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([alarm dup2 gethostbyaddr inet_ntoa memmove memset mkdir rmdir \
		realloc select seteuid strcasecmp strchr strcspn strerror \
		strncasecmp strrchr strspn strstr strtoul])

DIP_NET

# Check whether to debug things
AC_ARG_ENABLE([debug], AC_HELP_STRING([--enable-debug], [turn on debugging (default is NO)]),
	      [	if test "x${enable_debug}" = "xyes"; then
	      		use_debug="y"
			CFLAGS="-g -Wall $CFLAGS"
			AC_DEFINE([DEBUG], [1], [Are we debugging?])
			AC_DEFINE([DEBUG_MEMORY], [1], [Turn on expensive, but useful, memory debugging code])
		fi ])
if test -z "${use_debug}"; then
	AC_CHECK_FUNCS([strdup vasprintf vsnprintf])
fi

# Use SSL (currently OpenSSL)
AC_ARG_ENABLE([ssl], AC_HELP_STRING([--enable-ssl], [Add SSL support]),
              [ if test "x${enable_ssl}" = "xyes"; then
	                 use_ssl="y"
			 CFLAGS="-DSSL $CFLAGS "
                         AC_DEFINE([SSL], [1], [Try to sniff me now])
                fi ])
# if test -z "${use_ssl}"; then
#  AC_CHECK_FUNCS([str	 dup vasprintf vsnprintf])
# fi
			

# Use of poll() can be disabled in favour of select
AC_ARG_ENABLE([poll], AC_HELP_STRING([--disable-poll], [disable use of the poll() function (default is NO)]),
	      [	if test "x${enable_poll}" = "xyes"; then
	      		:
	      	else
			use_poll="n"
		fi ])
if test -z "${use_poll}"; then
	AC_CHECK_FUNCS([poll])
	AC_CHECK_HEADERS([poll.h sys/poll.h])
fi

# Use of select() can be disabled too
AC_ARG_ENABLE([select], AC_HELP_STRING([--disable-select], [disable use of the select() function (default is NO)]),
	      [	if test "x${enable_select}" = "xyes"; then
	      		:
		else
			use_select="n"
		fi ])
if test -z "${use_select}"; then
	AC_CHECK_FUNCS([select])
fi

AC_CONFIG_FILES([Makefile conf/Makefile contrib/Makefile getopt/Makefile crypt/Makefile doc/Makefile src/Makefile])
AC_OUTPUT
