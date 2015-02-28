#
# Copyright 2001-2008 Double Precision, Inc.  See COPYING for
# distribution information.


AM_CPPFLAGS=@CPPAUTH@

CLEANFILES=sysconfdir.h uids.h calendardir.h uids

DISTCLEANFILES=pcp.libs

SUBDIRS=po

noinst_LTLIBRARIES=libpcp.la

noinst_PROGRAMS=pcp pcpd

EXTRA_DIST= ../../config.rpath m4/ChangeLog README.html $(srcdir)/m4/*.m4

BUILT_SOURCES=calendardir.h sysconfdir.h uids.h

libpcp_la_SOURCES=pcp.h pcpacl.c pcpdir.c pcpgmtime.c pcplocale.c \
	pcpaddcommon.c pcpparseymd.c pcpmksocket.c pcpnet.c uids.c
libpcp_la_LIBADD=
libpcp_la_LDFLAGS=-static

pcp_SOURCES=pcp.c
pcp_DEPENDENCIES=libpcp.la ../rfc2045/librfc2045.la ../rfc822/librfc822.la \
	../numlib/libnumlib.la

pcp_LDADD=$(pcp_DEPENDENCIES) -lcourier-unicode @LIBINTL@ @EFENCE@ @NETLIBS@

pcpd_SOURCES=pcpd.c pcpdauth.c pcpdauth.h pcpdauthme.c pcpdauthtoken.c \
	pcpdauthtoken.h pcpdtimer.c pcpdtimer.h

COMMONLIBS=libpcp.la ../maildir/libmaildir.la \
	../rfc2045/librfc2045.la ../rfc822/librfc822.la \
	../numlib/libnumlib.la \
	../random128/librandom128.la ../libhmac/libhmac.la \
	../md5/libmd5.la ../sha1/libsha1.la\
	../liblock/liblock.la

pcpd_DEPENDENCIES=$(COMMONLIBS)
pcpd_LDADD=$(COMMONLIBS) @LDAUTH@ -lcourier-unicode -lcourierauth @LIBINTL@ @EFENCE@ @NETLIBS@

calendardir.h: config.status
	echo '#define CALENDARDIR "$(calendardir)"' >calendardir.h
	echo '#define LOCKFILE CALENDARDIR "/50pcpdlocal.lock"' >>calendardir.h
	echo '#define PIDFILE CALENDARDIR "/50pcpdlocal.pid"' >>calendardir.h
	echo '#define RANDTOKENFILE CALENDARDIR "/50pcpdlocal.tokens"' >>calendardir.h
	echo '#define PUBDIR CALENDARDIR "/public"' >>calendardir.h
	echo '#define PRIVDIR CALENDARDIR "/private"' >>calendardir.h
	echo '#define CACHEDIR "@localpcpdcachedir@"' >>calendardir.h
	echo '#define TIMEOUT @TIMEOUT@' >>calendardir.h
	echo '#define SENDIT "@SENDIT@"' >>calendardir.h
	if test ! -d $(srcdir)/../courier ; then echo '#define HOSTNAMEFILE SYSCONFDIR "/hostname"' >>calendardir.h ; echo '#define HOSTNAMEFILE2 "/etc/HOSTNAME"' >>calendardir.h ; else echo '#define HOSTNAMEFILE SYSCONFDIR "/defaultdomain"' >>calendardir.h ; echo '#define HOSTNAMEFILE2 SYSCONFDIR "/me"' >>calendardir.h ; fi

sysconfdir.h: config.status
	echo '#define SYSCONFDIR "$(sysconfdir)"' >sysconfdir.h

install-exec-local:
	-$(mkinstalldirs) $(DESTDIR)$(calendardir)/public
	-$(mkinstalldirs) $(DESTDIR)$(calendardir)/private
	-$(mkinstalldirs) $(DESTDIR)$(localpcpdcachedir)
	chmod 755 $(DESTDIR)$(calendardir)
	chmod 755 $(DESTDIR)$(calendardir)/public
	chmod 750 $(DESTDIR)$(calendardir)/private
	chmod 700 $(DESTDIR)$(localpcpdcachedir)
	-chown @localcacheowner@ $(DESTDIR)$(localpcpdcachedir)
	-chgrp -R @mailgroup@ $(DESTDIR)$(calendardir)
	echo "localcacheowner=@localcacheowner@" >uids
	echo "calendardir=$(calendardir)" >>uids
	echo "mailgroup=@mailgroup@" >>uids

install-perms:
	@$(MAKE) install-perms-recursive
	@$(MAKE) install-perms-local

uids.h: config.status
	echo '#define uid "@localcacheowner@"' >uids.h
	echo '#define gid "@mailgroup@"' >>uids.h

install-perms-recursive:
	@>permissions.dat
	@SUBDIRS="$(SUBDIRS)" ; for f in $$SUBDIRS ; do ( cd $$f && $(MAKE) $(AM_MAKEFLAGS) install-perms ) ; cat /dev/null >>$$f/permissions.dat ; cat $$f/permissions.dat >>permissions.dat ; done

install-perms-local:
	@echo $(DESTDIR)$(calendardir) 755 @localcacheowner@ @mailgroup@ >>permissions.dat
	@echo $(DESTDIR)$(calendardir)/public 755 @localcacheowner@ @mailgroup@ >>permissions.dat
	@echo $(DESTDIR)$(calendardir)/private 750 @localcacheowner@ @mailgroup@ >>permissions.dat
	@echo $(DESTDIR)$(localpcpdcachedir) 700 @localcacheowner@ @mailgroup@ >>permissions.dat

ACLOCAL_AMFLAGS = -I m4
