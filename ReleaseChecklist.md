**_Note:_** List is for old site, needs updating for google code host.

# Release checklist #

  1. Perform a `make maintainer-clean` and remove any litter
  1. Check it compiles and runs --enable-debug
  1. Perform a `make maintainer-clean` and remove any litter
  1. Check it compiles and runs without any flags
  1. Fix any bugs/warnings, commit them, and return to 1
  1. Perform a `make maintainer-clean` and remove any litter
  1. Run `cvs2cl --gmt --prune -U AUTHORS.map -l '-b'` to create new ChangeLog.  If this is a non-head branch replace the "-l 'b'" with "-F BRANCH-x\_x".
  1. `rm !ChangeLog.bak`
  1. `cvs diff source:trunk/ChangeLog > changes.txt`
  1. Edit changes.txt and undiffify it
  1. Read changes.txt and write summary.txt from it
  1. Write release.txt including summary.txt after an "-----Official Summary-----" marker and blank line.
  1. Edit the NEWS file, add in new version and any config/use changes
  1. Adjust the Features list in the README file if necessary.  This will also require the adjustment of the website's index.html.in file.
  1. Any changes to INSTALL?  Do them and adjust website's install.html file.
  1. Do a general `cvs commit -m "Version x.x.x released."`
  1. do a `cvs tag RELEASE-x_x_x`
  1. do an `autogen.sh` without any flags
  1. make dist
  1. `gpg --armor --comment 'See http://dircproxy.securiweb.net/DircproxyDownload for more information' --detach-sign DISTFILE`
  1. `md5sum DISTFILE > DISTFILE.md5`
  1. scp dist file, md5 file and asc file to tetris:~ftp/pub/dircproxy/unstable
  1. Remove 'src' directory from website and replace with new version
  1. Add new news item title "dircproxy x.x.x released" containing release.txt
  1. Add new version to bugzilla's dircproxy product
  1. Run build-site.pl to make the site carry the latest information.
  1. Check the site front page, news page and download page.
  1. Announce new version on freshmeat using summary.txt
  1. E-mail dircproxy-announce list with release.txt making sure to sign the e-mail.
  1. E-Mail lester@mazpe.net with any significant changes that could affect packaging (new files etc), making sure to sign the e-mail.
  1. Update and upload the new Debian package to master
  1. Edit source:trunk/configure.ac & source:trunk/contrib/dircproxy.spec and increment the version number, commit with message "SVN version now x.x.x"


---

'''To remove litter:'''
```
for A in . conf contrib crypt doc getopt src; do \
		(cd $A; rm -rf `cat .cvsignore`; rm -rf *~); done
```