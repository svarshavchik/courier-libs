#
# Copyright 1998 - 2015 Double Precision, Inc.  See COPYING for
# distribution information.

AM_CPPFLAGS=@CPPAUTH@

BUILT_SOURCES=README.proxy

DISTCLEANFILES=imapd.pam pop3d.pam imapd.cnf pop3d.cnf
CLEANFILES=imapd.8 imapd.html mkimapdcert.html mkimapdcert.8 \
	makeimapaccess.html makeimapaccess.8 \
	mkdhparams.html mkdhparams.8 \
	courierpop3d.html courierpop3d.8 mkpop3dcert.html mkpop3dcert.8

EXTRA_DIST=testsuite testsuite.txt smaptestsuite smaptestsuite.txt \
	testsuitevalgrind \
	BUGS BUGS.html common.authpam imapd.authpam \
	pop3d.authpam system-auth.authpam system-auth2.authpam\
	makeimapaccess.8.in makeimapaccess.html.in \
	imapd.html.in imapd.8.in \
	mkdhparams.html.in mkdhparams.8.in \
	mkimapdcert.html.in mkimapdcert.8.in \
	mkpop3dcert.html.in mkpop3dcert.8.in \
	courierpop3d.html.in courierpop3d.8.in \
	README.proxy README.proxy.html \
	imapd.cnf.gnutls pop3d.cnf.gnutls \
	pop3testsuite pop3testsuite.txt

noinst_SCRIPTS=mkimapdcert mkpop3dcert mkdhparams
noinst_PROGRAMS=imaplogin imapd imapd.testmockpurge pop3login pop3d \
	pop3d.testmock

noinst_DATA=imapd.8 imapd.html imapd.cnf pop3d.cnf \
	makeimapaccess.html makeimapaccess.8 \
	mkdhparams.html mkdhparams.8 \
	mkimapdcert.html mkimapdcert.8 \
	mkpop3dcert.html mkpop3dcert.8 \
	courierpop3d.html courierpop3d.8

imapd.cnf: imapd.cnf.@ssllib@
	cp imapd.cnf.@ssllib@ imapd.cnf
	touch imapd.cnf

imapd.cnf: $(top_builddir)/config.status

pop3d.cnf: pop3d.cnf.@ssllib@
	cp pop3d.cnf.@ssllib@ pop3d.cnf
	touch pop3d.cnf

pop3d.cnf: $(top_builddir)/config.status

noinst_LTLIBRARIES=libimaplogin.la libimapd.la libpop3d.la
libimaplogin_la_SOURCES=
libimaplogin_la_LIBADD=../tcpd/libspipe.la ../tcpd/libtlsclient.la \
	../numlib/libnumlib.la
libimaplogin_la_LDFLAGS=-static

libimapd_la_SOURCES=mainloop.c imaptoken.c imaptoken.h imapwrite.c \
	imapwrite.h capability.c externalauth.c smap.c smapsnapshot.c
libimapd_la_LIBADD= ../rfc2045/librfc2045.la ../maildir/libmaildir.la \
	../rfc822/librfc822.la ../liblock/liblock.la ../numlib/libnumlib.la \
	-lcourier-unicode
libimapd_la_LDFLAGS=-static

imaplogin_SOURCES=imaplogin.c authenticate_auth.c proxy.c proxy.h
imaplogin_DEPENDENCIES=libimapd.la libimaplogin.la
imaplogin_LDADD=libimapd.la libimaplogin.la \
	@LDAUTH@ -lcourierauth -lcourierauthsasl

imapd_SOURCES=fetch.c fetchinfo.c fetchinfo.h imapd.c imapd.h \
	imapscanclient.c imapscanclient.h \
	mailboxlist.c mailboxlist.h \
	msgbodystructure.c msgenvelope.c \
	mysignal.c mysignal.h \
	outbox.c outbox.h \
	thread.c thread.h \
	search.c searchinfo.c searchinfo.h \
	storeinfo.c storeinfo.h

imapd_DEPENDENCIES=libimapd.la @dblibrary@

imapd_LDADD=libimapd.la	@dblibrary@ @DEBUGLIB@ @LDAUTH@ -lcourierauth

imapd_testmockpurge_SOURCES=$(imapd_SOURCES) mockmaildirpurge.c
imapd_testmockpurge_DEPENDENCIES=$(imapd_DEPENDENCIES)
imapd_testmockpurge_LDADD=$(imapd_LDADD)

pop3login_SOURCES=pop3login.c pop3dcapa.c proxy.c proxy.h
pop3login_DEPENDENCIES=../tcpd/libspipe.la ../tcpd/libspipe.la libpop3d.la
pop3login_LDADD=../tcpd/libtlsclient.la ../tcpd/libspipe.la libpop3d.la ../tcpd/libspipe.la @LDAUTH@ -lcourierauth -lcourierauthsasl @NETLIBS@

libpop3d_la_SOURCES=externalauth.c
libpop3d_la_LIBADD=../maildir/libmaildir.la \
	../rfc2045/librfc2045.la \
	../rfc822/librfc822.la \
	../numlib/libnumlib.la
libpop3d_la_DEPENDENCIES=$(libpop3d_la_LIBADD)
libpop3d_la_LDFLAGS=-static

pop3d_SOURCES=pop3dserver.c pop3dcapa.c
pop3d_DEPENDENCIES=libpop3d.la
pop3d_LDADD=libpop3d.la @LDAUTH@ -lcourierauth -lcourier-unicode

pop3d_testmock_SOURCES=testpop3dserver.c pop3dcapa.c
pop3d_testmock_DEPENDENCIES=libpop3d.la
pop3d_testmock_LDADD=libpop3d.la @LDAUTH@ -lcourierauth -lcourier-unicode

clean-local:
	rm -rf $(DEPDIR)

BUGS: BUGS.html
	elinks -dump -no-numbering BUGS.html >BUGS

imapd.html: imapd.html.in
	./config.status --file=imapd.html

imapd.8: imapd.8.in
	./config.status --file=imapd.8

makeimapaccess.html: makeimapaccess.html.in
	./config.status --file=makeimapaccess.html

makeimapaccess.8: makeimapaccess.8.in
	./config.status --file=makeimapaccess.8

mkdhparams.html: mkdhparams.html.in
	./config.status --file=mkdhparams.html

mkdhparams.8: mkdhparams.8.in
	./config.status --file=mkdhparams.8

mkimapdcert.html: mkimapdcert.html.in
	./config.status --file=mkimapdcert.html

mkimapdcert.8: mkimapdcert.8.in
	./config.status --file=mkimapdcert.8

mkpop3dcert.html: mkpop3dcert.html.in
	./config.status --file=mkpop3dcert.html

mkpop3dcert.8: mkpop3dcert.8.in
	./config.status --file=mkpop3dcert.8

courierpop3d.html: courierpop3d.html.in
	./config.status --file=courierpop3d.html

courierpop3d.8: courierpop3d.8.in
	./config.status --file=courierpop3d.8

BUILT_SOURCES += imapd.dist imapd-ssl.dist pop3d.dist pop3d-ssl.dist
if HAVE_SGML

GITFILES=imapd.dist.in imapd-ssl.dist.in pop3d.dist.in pop3d-ssl.dist.in

BUILT_SOURCES += $(GITFILES)

$(GITFILES): %: %.git
	perl -p -e 's/\$$Id:[^\$$]*/\$$Id: '"`git log --pretty=format:\"%H\" -n 1 -- $<`-`date +%Y%m%d%H%M%S`/" <$< >$@.tmp
	mv -f $@.tmp $@

imapd.html.in: imapd.sgml ../docbook/sgml2html
	../docbook/sgml2html imapd.sgml imapd.html.in

imapd.8.in: imapd.sgml ../docbook/sgml2man
	../docbook/sgml2man imapd.sgml imapd.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv imapd.8 imapd.8.in

makeimapaccess.html.in: makeimapaccess.sgml ../docbook/sgml2html
	../docbook/sgml2html makeimapaccess.sgml makeimapaccess.html.in

makeimapaccess.8.in: makeimapaccess.sgml ../docbook/sgml2man
	../docbook/sgml2man makeimapaccess.sgml makeimapaccess.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv makeimapaccess.8 makeimapaccess.8.in

mkdhparams.html.in: mkdhparams.sgml ../docbook/sgml2html
	../docbook/sgml2html mkdhparams.sgml mkdhparams.html.in

mkdhparams.8.in: mkdhparams.sgml ../docbook/sgml2man
	../docbook/sgml2man mkdhparams.sgml mkdhparams.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv mkdhparams.8 mkdhparams.8.in

mkimapdcert.html.in: mkimapdcert.sgml ../docbook/sgml2html
	../docbook/sgml2html mkimapdcert.sgml mkimapdcert.html.in

mkimapdcert.8.in: mkimapdcert.sgml ../docbook/sgml2man
	../docbook/sgml2man mkimapdcert.sgml mkimapdcert.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv mkimapdcert.8 mkimapdcert.8.in

mkpop3dcert.html.in: mkpop3dcert.sgml ../docbook/sgml2html
	../docbook/sgml2html mkpop3dcert.sgml mkpop3dcert.html.in

mkpop3dcert.8.in: mkpop3dcert.sgml ../docbook/sgml2man
	../docbook/sgml2man mkpop3dcert.sgml mkpop3dcert.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv mkpop3dcert.8 mkpop3dcert.8.in

courierpop3d.html.in: courierpop3d.sgml ../docbook/sgml2html
	../docbook/sgml2html courierpop3d.sgml courierpop3d.html.in

courierpop3d.8.in: courierpop3d.sgml ../docbook/sgml2man
	../docbook/sgml2man courierpop3d.sgml courierpop3d.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv courierpop3d.8 courierpop3d.8.in

README.proxy.html: README.proxy.sgml
	rm -rf html.tmp
	xsltproc -o html.tmp/ --nonet http://docbook.sourceforge.net/release/xsl/current/xhtml/onechunk.xsl README.proxy.sgml
	xsltproc --nonet ../docbook/fixhtml.xsl html.tmp/* >README.proxy.html.tmp
	mv -f README.proxy.html.tmp README.proxy.html
	rm -rf html.tmp

README.proxy: README.proxy.html
	elinks -dump -no-numbering README.proxy.html >README.proxy
endif

check-am:
	@test "@MAKECHECKBROKEN@" = "Y" || exit 0; echo "" ; echo "Error: --with-trashquota or the --enable-workarounds-for-imap-client-bugs" ; echo "option was specified to the configure script."; echo ""; echo "As INSTALL told you, make check fails if these options are used, and I wasn't"; echo "kidding when I wrote it.  Reconfigure and rebuild without these options, then"; echo "rerun make and make check.  If make check passes, reconfigure again with your"; echo "original options, and proceed with installing this server.  Have fun!"; exit 1
	@rm -f conftest1
	@chmod +x testsuitefix.pl
	unset IMAP_USELOCKS; LC_ALL=C; export LC_ALL; $(srcdir)/testsuite | ./testsuitefix.pl | sort | diff -U 3 $(srcdir)/testsuite.txt -
	IMAP_USELOCKS=1; export IMAP_USELOCKS; LC_ALL=C; export LC_ALL; $(srcdir)/testsuite | ./testsuitefix.pl | sort | diff -U 3 $(srcdir)/testsuite.txt -
	unset IMAP_USELOCKS; LC_ALL=C; export LC_ALL; test "@smap@" = "yes" || exit 0; export PERL=@PERL@; @SHELL@ $(srcdir)/smaptestsuite | ./testsuitefix.pl | sort | diff -U 3 $(srcdir)/smaptestsuite.txt -
	IMAP_USELOCKS=1; export IMAP_USELOCKS; LC_ALL=C; export LC_ALL; test "@smap@" = "yes" || exit 0; export PERL=@PERL@; @SHELL@ $(srcdir)/smaptestsuite | ./testsuitefix.pl | sort | diff -U 3 $(srcdir)/smaptestsuite.txt -
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		IMAP_USELOCKS=1; export IMAP_USELOCKS; \
		LC_ALL=C; export LC_ALL; \
		rm -f confmdmemleak; \
		IMAPINVOKE='@SHELL@ $(srcdir)/testsuitevalgrind ./imapd'; export IMAPINVOKE; \
		@SHELL@ -x $(srcdir)/testsuite 2>&1 | tee confmderror.txt; test -f confmdmemleak || exit 0; exit 1
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		IMAP_USELOCKS=1; export IMAP_USELOCKS; \
		LC_ALL=C; export LC_ALL; \
		rm -f confmdmemleak; \
		IMAPINVOKE='@SHELL@ $(srcdir)/testsuitevalgrind ./imapd'; export IMAPINVOKE; \
		export PERL=@PERL@; @SHELL@ -x $(srcdir)/smaptestsuite 2>&1 | tee confmderror.txt; test -f confmdmemleak || exit 0; exit 1
	rm -f confmderror.txt
	rm -rf confmdtest
	@SHELL@ $(srcdir)/pop3testsuite | sed 's/^--=_.*/--mimeboundary/;s/ENOMEM.*/ENOMEM/;s/boundary=.*/boundary=/' | diff -U 3 $(srcdir)/pop3testsuite.txt -
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
	   rm -f confmdmemleak; \
	   POP3INVOKE='@SHELL@ $(srcdir)/testsuitevalgrind ./pop3d.testmock' \
           @SHELL@ -x $(srcdir)/pop3testsuite; \
           test -f confmdmemleak || exit 0; exit 1

testsuite-imap:
	@chmod +x testsuitefix.pl
	@LC_ALL=C; export LC_ALL; $(srcdir)/testsuite | ./testsuitefix.pl | sort

testsuite-smap:
	@chmod +x testsuitefix.pl
	@LC_ALL=C; export LC_ALL; test "@smap@" = "yes" || exit 0; export PERL=@PERL@; @SHELL@ $(srcdir)/smaptestsuite | ./testsuitefix.pl | sort

testsuite-pop3:
	@LC_ALL=C @SHELL@ pop3testsuite | sed 's/^--=_.*/--mimeboundary/;s/ENOMEM.*/ENOMEM/;s/boundary=.*/boundary=/'

check-version:
	test "$(VERSION)" = "$(TOPLEVEL_VERSION)" && exit 0; \
		echo "Makefile.am version mismatch"; exit 1
