#
# Copyright 1998 - 2011 Double Precision, Inc.  See COPYING for
# distribution information.

@SET_MAKE@


AM_CPPFLAGS=@CPPAUTH@

CLEANFILES=htmllibdir.h ldapsearch webgpg ldapaddressbook.dist \
	cron.cmd sqwebmail.msg TIMEZONELIST
DISTCLEANFILES=webmail.authpam

SUBDIRS=images html

scriptdir=@scriptdir@
script_SCRIPTS=sendit.sh cleancache.pl ldapsearch webgpg
sysconf_DATA=ldapaddressbook.dist sqwebmaild.dist
pkglibexecdir=@pkglibexecdir@
pkglibexec_PROGRAMS=sqwebpasswd

DOCS=SECURITY BUGS TODO SECURITY.html BUGS.html TODO.html
noinst_DATA=$(DOCS) TIMEZONELIST

#
#  Because cgibindir is not based on prefix, we must conditionally install
#  bin_PROGRAMS in install-exec-local, otherwise make distcheck will fail.
#

noinst_PROGRAMS=sqwebmail sqwebmaild showmsg2html testhtml

COMMONDEPENDENCIES=$(LIBOBJS) ../pcp/libpcp.la ../maildir/libmaildir.la \
	../rfc2045/librfc2045.la ../rfc822/librfc822.la \
	../rfc822/libencode.la \
	../liblock/liblock.la \
	../http11/libhttp11.la ../cgi/libcgi.la \
	../ldapaddressbook/libaddressbook.la \
	../gpglib/libgpg.la \
	../random128/librandom128.la ../md5/libmd5.la \
	../numlib/libnumlib.la \
	@dblibrary@
COMMONLDADD=$(COMMONDEPENDENCIES) `cat ../pcp/pcp.libs` -lcourier-unicode

noinst_LTLIBRARIES=libwebmail.la

libwebmail_la_SOURCES=filter.c filter.h msg2html.c msg2html.h html.c html.h \
			buf.c buf.h
libwebmail_la_DEPENDENCIES=$(COMMONDEPENDENCIES)
libwebmail_la_LIBADD=$(COMMONLDADD)
libwebmail_la_LDFLAGS=-static

showmsg2html_SOURCES=showmsg2html.c
showmsg2html_LDADD=libwebmail.la
showmsg2html_LDFLAGS=-static

ISPELLSOURCES=sqispell.c ispell.c ispell.h

BUILT_SOURCES=htmllibdir.h
if HAVE_SGML

GITFILES=sqwebmaild.dist.in

BUILT_SOURCES += $(GITFILES)

$(GITFILES): %: %.git
	perl -p -e 's/\$$Id:[^\$$]*/\$$Id: '"`git log --pretty=format:\"%H\" -n 1 -- $<`-`date +%Y%m%d%H%M%S`/" <$< >$@.tmp
	mv -f $@.tmp $@
endif

EXTRA_DIST=$(DOCS) sqwebmail.pamconf sqwebmail-system-auth.pamconf\
	 sqwebmail-system-auth2.pamconf\
           strdup.c strcasecmp.c strncasecmp.c sv-make_timezonelist.pl\
           $(ISPELLSOURCES) \
	testhtml.tst testhtml.txt

testhtml_SOURCES=testhtml.c
testhtml_LDADD=libwebmail.la

#CFLAGS += -Dmalloc=my_malloc -Drealloc=my_realloc -Dfree=my_free -Dstrdup=my_strdup -Dcalloc=my_calloc

if ISPELL
SQISPELLSOURCES=$(ISPELLSOURCES)
endif


sqwebmaild_SOURCES=acl.c acl.h \
		addressbook.c addressbook.h args.c attachments.c \
		auth.c auth.h autoresponse.c autoresponse.h \
		changepw.c \
		folder.c folder.h gpg.c gpg.h \
		ldaplist.c logindomainlist.c logindomainlist.h \
		maildir.c maildir.h mailfilter.c \
		mailfilter.h mailinglist.c mailinglist.h newmsg.c newmsg.h \
		newmsg_create.c	newmsg_newdraft.c pcp.c pcp.h pref.c pref.h \
		sqconfig.c sqconfig.h sqwebmail.c \
		strftime.c strftime.h \
		sqwebmail.h token.c token.h $(SQISPELLSOURCES)

sqwebmaild_LDADD=libwebmail.la `cat ../maildir/maildir.libdeps` \
	@NETLIBS@ @LDAUTH@ -lcourierauth -lpcre
sqwebmaild_DEPENDENCIES=libwebmail.la ../maildir/maildir.libdeps

sqwebmail_SOURCES=sqwebmaild.c
sqwebmail_DEPENDENCIES= ../cgi/libcgi.la
sqwebmail_LDADD=@NETLIBS@ ../cgi/libcgi.la
sqwebmail_LDFLAGS=-static

sqwebpasswd_SOURCES=sqwebpasswd.c
sqwebpasswd_LDADD=@NETLIBS@ @LDAUTH@ -lcourierauth

HTML2TXT=`which lynx 2>/dev/null && echo "lynx -dump -nolist" && exit; echo "links -dump"`

SECURITY: SECURITY.html
	rm -f SECURITY
	$(HTML2TXT) $(srcdir)/SECURITY.html >SECURITY

BUGS: BUGS.html
	rm -f BUGS
	$(HTML2TXT) $(srcdir)/BUGS.html >BUGS

TODO: TODO.html
	rm -f TODO
	$(HTML2TXT) $(srcdir)/TODO.html >TODO

TIMEZONELIST: sv-make_timezonelist.pl
	rm -f TIMEZONELIST
	-@PERL@ $(srcdir)/sv-make_timezonelist.pl

install-perms:
	@$(MAKE) install-perms-recursive
	@$(MAKE) install-perms-local

install-perms-recursive:
	@>permissions.dat
	@SUBDIRS="$(SUBDIRS)" ; for f in $$SUBDIRS ; do ( cd $$f && $(MAKE) $(AM_MAKEFLAGS) install-perms ) ; cat /dev/null >>$$f/permissions.dat ; cat $$f/permissions.dat >>permissions.dat ; done

install-perms-local: install-perms-cache
	@echo "$(scriptdir)/webgpg 755 bin bin" >>permissions.dat
	@echo "$(scriptdir)/sendit.sh 755 bin bin" >>permissions.dat
	@echo "$(scriptdir)/ldapsearch 755 bin bin" >>permissions.dat
	@echo "$(sysconfdir)/ldapaddressbook.dist 644 root bin config" >>permissions.dat

htmllibdir.h: config.status
	echo '#define HTMLLIBDIR "@htmldir@"' >htmllibdir.h
	echo '#define SENDITSH "$(scriptdir)/sendit.sh"' >>htmllibdir.h
	echo '#define CALENDARMODE "$(sysconfdir)/calendarmode"' >>htmllibdir.h
	echo '#define LOGINDOMAINLIST "$(sysconfdir)/logindomainlist"' >>htmllibdir.h
	echo '#define MAXMSGSIZE @MAXMSGSIZE@' >>htmllibdir.h
	echo '#define IMGPATH "@imageurl@"' >>htmllibdir.h
	echo '#define SOCKDIR "$(localstatedir)"' >>htmllibdir.h
	echo '#define SHAREDINDEXFILE "$(sysconfdir)/shared/index"' >>htmllibdir.h
	echo '#define SQWEBPASSWD "$(pkglibexecdir)/sqwebpasswd"' >>htmllibdir.h
	if test -d $(srcdir)/../../courier \
		; then \
		echo '#define HOSTNAMEFILE "$(sysconfdir)/defaultdomain"' >>htmllibdir.h \
		; echo '#define MAKEMIME "$(bindir)/makemime"' >>htmllibdir.h \
		; else \
		echo '#define HOSTNAMEFILE "$(sysconfdir)/hostname"' >>htmllibdir.h \
		; echo '#define MAKEMIME "$(pkglibexecdir)/makemime"' >>htmllibdir.h \
		; fi
	echo '#define LDAPADDRESSBOOK "$(sysconfdir)/ldapaddressbook"' >>htmllibdir.h
	echo '#define LDAPSEARCH "$(scriptdir)/ldapsearch"' >>htmllibdir.h
	echo '#define AUTHCHARSET "$(sysconfdir)/authcharset"' >>htmllibdir.h

ldapsearch: ../ldapaddressbook/ldapsearch
	cp ../ldapaddressbook/ldapsearch .

webgpg: ../gpglib/webgpg
	cp ../gpglib/webgpg .

ldapaddressbook.dist: ../ldapaddressbook/ldapaddressbook.dist
	cp $(srcdir)/../ldapaddressbook/ldapaddressbook.dist .

install-data-local: cache-reminder

install-exec-hook:
	-chown @mailuser@ $(DESTDIR)$(pkglibexecdir)/sqwebpasswd
	-chgrp @mailgroup@ $(DESTDIR)$(pkglibexecdir)/sqwebpasswd
	-chmod 755 $(DESTDIR)$(pkglibexecdir)/sqwebpasswd
	-chmod g+s $(DESTDIR)$(pkglibexecdir)/sqwebpasswd

configure-check:
	@echo "URL to the image directory is @imageurl@"

install-perms-cache:
	@echo "$(scriptdir)/cleancache.pl 755 bin bin" >>permissions.dat
	@echo "@cachedir@ 700 @cacheowner@ bin" >>permissions.dat

cache-reminder:
	-$(mkinstalldirs) $(DESTDIR)@cachedir@
	-$(mkinstalldirs) $(DESTDIR)$(piddir)
	test -w /etc || exit 0; chown @cacheowner@ $(DESTDIR)@cachedir@
	@rm -f cron.cmd
	@echo '#! @SHELL@' >cron.cmd
	@echo 'su -p -c "$(scriptdir)/cleancache.pl" @cacheowner@' >>cron.cmd
	@chmod 555 cron.cmd
	@cp /dev/null sqwebmail.msg
	@echo '-----------------------------------------------------------' >>sqwebmail.msg
	@echo 'You must now add the following command to your system startup script:' >>sqwebmail.msg
	@echo '' >>sqwebmail.msg
	@if test -d $(srcdir)/../../courier \
		; then \
	echo '$(sbindir)/webmaild start' >>sqwebmail.msg ; else \
	echo '$(libexecdir)/sqwebmaild.rc start' >>sqwebmail.msg ; fi
	@echo '' >>sqwebmail.msg
	@echo 'You must now create the following cron job that runs at' >>sqwebmail.msg
	@echo 'regular intervals:' >>sqwebmail.msg
	@echo '' >>sqwebmail.msg
	@echo 'su -c "$(scriptdir)/cleancache.pl" @cacheowner@' >>sqwebmail.msg
	@echo '' >>sqwebmail.msg
	@echo '-----------------------------------------------------------' >>sqwebmail.msg
	@echo '(note - your su command may use slightly different arguments)' >>sqwebmail.msg
	@echo '' >>sqwebmail.msg
	@echo '' >>sqwebmail.msg


check-am:
	./testhtml <$(srcdir)/testhtml.tst | diff -U 3 $(srcdir)/testhtml.txt -
