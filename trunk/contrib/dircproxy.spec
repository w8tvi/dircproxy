Summary: irc proxy
Name: dircproxy
%define branch 1.2
%define	version %{branch}.0
%define location /usr
Version: %{version}
Release: 1
Copyright: GPL
Group: Applications/Internet
URL: http://dircproxy.googlecode.com/
Source: http://dircproxy.googlecode.com/files/dircproxy-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-root
Packager: Hollis Blanchard <hollis@yellowdoglinux.com>

%description
dircproxy is an IRC proxy server designed for people who use IRC from lots of
different workstations or clients, but wish to remain connected and see what
they missed while they were away. You connect to IRC through dircproxy, and it
keeps you connected to the server, even after you detach your client from it.
While you're detached, it logs channel and private messages as well as
important events, and when you reattach it'll let you know what you missed. 

%prep
%setup

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{location}
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{location}/bin
mkdir -p $RPM_BUILD_ROOT/%{location}/share/dircproxy
mkdir -p $RPM_BUILD_ROOT/%{location}/man/man1/

install src/dircproxy		$RPM_BUILD_ROOT/%{location}/bin
install crypt/dircproxy-crypt	$RPM_BUILD_ROOT/%{location}/bin
install conf/dircproxyrc	$RPM_BUILD_ROOT/%{location}/share/dircproxy/
install contrib/log.pl		$RPM_BUILD_ROOT/%{location}/share/dircproxy/
install doc/dircproxy-crypt.1	$RPM_BUILD_ROOT/%{location}/man/man1/
install doc/dircproxy.1		$RPM_BUILD_ROOT/%{location}/man/man1/

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{location}/bin/dircproxy
%{location}/bin/dircproxy-crypt
%{location}/share/dircproxy/dircproxyrc
%{location}/share/dircproxy/log.pl
%{location}/man/man1/dircproxy-crypt.1*
%{location}/man/man1/dircproxy.1*
%doc AUTHORS COPYING ChangeLog FAQ INSTALL NEWS PROTOCOL README* TODO
