#
# Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
# distribution information.

AM_CFLAGS=@PCRE_CFLAGS@

noinst_LTLIBRARIES=libmaildir.la


DOCS=	deliverquota.html.in deliverquota.8.in \
	maildiracl.1.in \
	maildir.html maildir.5 \
	maildiracl.html.in \
	maildirmake.html.in maildirmake.1.in maildirquota.html maildirquota.7 \
	maildirwatch.html maildirwatch.1 \
	maildirkw.html maildirkw.1

BUILT_SOURCES=maildirsharedrc.h maildirfilterconfig.h quotawarnmsg.h \
	mailbot.h autoresponsequota.h $(noinst_DATA) libmaildir.deps

if HAVE_SGML
BUILT_SOURCES += $(DOCS)
endif

noinst_DATA=deliverquota.html maildirmake.html deliverquota.8 maildirmake.1 \
	maildiracl.html maildiracl.1

libmaildir.deps: config.status
	echo "$(PCRE_LDFLAGS)" >libmaildir.deps

libmaildir_la_SOURCES=autoresponse.C autoresponse.h \
	maildiraclt.h maildiracltcpp.C \
	maildircache.C maildircache.h \
	maildircreate.C maildircreate.h \
	maildirfilename.C maildirgetnew.c \
	maildirfilter.C maildirfilter2.C \
	maildirfilter.h maildirfiltertypelist.h\
	maildirflags.c maildirmkdir.c \
	maildirgetquota.c maildirgetquota.h \
	maildirinfo.c maildirinfo.h maildirinfo2.C \
	maildirinfocpp.C \
	maildirkeywords.C maildirkeywords.h maildirlist.C maildirlock.C \
	maildirmake2.C \
	maildirnewshared.C maildirnewshared.h maildirnewshared2.C \
	maildiropen.C maildirparsequota.C \
	maildirpathcpp.C maildirpurgetmp.c maildirmisc.h \
	maildirrename.c \
	maildirsearch.c maildirsearchC.cpp maildirsearch.h \
	maildirshared.C maildirshared2.C \
	maildirquota.c maildirquota.h maildirrequota.c maildirrequota.h \
	maildirwatch.C maildirwatch.h loginexec.c loginexec.h

noinst_PROGRAMS=deliverquota maildirmake testmaildirfilter maildirkwtest \
	maildirkwtest2 maildirkwtest3 testautoresponse testmaildirmisc \
	maildirkw maildiracl maildiraclttest testmaildirsearch maildirwatch \
	maildirmake.tst

deliverquota_SOURCES=deliverquota.c
deliverquota_DEPENDENCIES=libmaildir.la ../rfc822/librfc822.la \
				../numlib/libnumlib.la
deliverquota_LDADD=libmaildir.la ../rfc822/librfc822.la ../numlib/libnumlib.la
deliverquota_LDFLAGS=-static
nodist_EXTRA_deliverquota_SOURCES=dummy.C

maildirmake_SOURCES=maildirmake.C
maildirmake_DEPENDENCIES=libmaildir.la \
			../numlib/libnumlib.la \
			../rfc822/librfc822.la
maildirmake_LDADD=libmaildir.la \
			../numlib/libnumlib.la \
			../rfc822/librfc822.la -lcourier-unicode @PCRE_LDFLAGS@
maildirmake_LDFLAGS=-static

maildirmake_tst_SOURCES=maildirmake.tst.C
maildirmake_tst_DEPENDENCIES=$(maildirmake_DEPENDENCIES)
maildirmake_tst_LDADD=$(maildirmake_LDADD)
maildirmake_tst_LDFLAGS=$(maildirmake_LDFLAGS)


testmaildirfilter_SOURCES=maildirfiltertypelist.h testmaildirfilter.C
testmaildirfilter_DEPENDENCIES=libmaildir.la ../numlib/libnumlib.la
testmaildirfilter_LDADD=libmaildir.la ../numlib/libnumlib.la -lcourier-unicode  @PCRE_LDFLAGS@
testmaildirfilter_LDFLAGS=-static
nodist_EXTRA_testmaildirfilter_SOURCES=dummy.C

maildirkw_SOURCES=maildirkw.C
maildirkw_LDADD=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildirkw_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
	../numlib/libnumlib.la
maildirkw_LDFLAGS=-static

maildirkwtest_SOURCES=maildirkwtest.C
maildirkwtest_LDADD=$(maildirkw_LDADD) -lcourier-unicode
maildirkwtest_DEPENDENCIES=$(maildirkw_DEPENDENCIES)
maildirkwtest_LDFLAGS=-static

maildirkwtest2_SOURCES=maildirkw.C maildirkwtest2.C
maildirkwtest2_LDADD=$(maildirkw_LDADD)
maildirkwtest2_DEPENDENCIES=$(maildirkw_DEPENDENCIES)
maildirkwtest2_LDFLAGS=-static

maildirkwtest3_SOURCES=maildirkwtest3.C
maildirkwtest3_LDADD=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildirkwtest3_DEPENDENCIES=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildirkwtest3_LDFLAGS=-static

maildirwatch_SOURCES=maildirwatchprog.c
maildirwatch_LDADD=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildirwatch_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
	../numlib/libnumlib.la
maildirwatch_LDFLAGS=-static
nodist_EXTRA_maildirwatch_SOURCES=dummy.C

maildiracl=maildiracl.c
maildiracl_LDADD=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildiracl_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
		../numlib/libnumlib.la
maildiracl_LDFLAGS=-static
nodist_EXTRA_maildiracl_SOURCES=dummy.C

maildiraclttest_SOURCES=testmaildiraclt.C
maildiraclttest_LDADD=libmaildir.la ../liblock/liblock.la \
		../numlib/libnumlib.la
maildiraclttest_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
		../numlib/libnumlib.la
maildiraclttest_LDFLAGS=-static

testmaildirsearch_SOURCES=testmaildirsearch.c
testmaildirsearch_LDADD=libmaildir.la -lcourier-unicode
testmaildirsearch_DEPENDENCIES=libmaildir.la

testautoresponse_SOURCES=testautoresponse.C
testautoresponse_LDADD=libmaildir.la -lcourier-unicode
testautoresponse_DEPENDENCIES=libmaildir.la

testmaildirmisc_SOURCES=testmaildirmisc.C
testmaildirmisc_LDADD=libmaildir.la -lcourier-unicode
testmaildirmisc_DEPENDENCIES=libmaildir.la

EXTRA_DIST=README.maildirquota.html README.maildirquota.txt \
	testsuitevalgrind \
	README.imapkeywords.html \
	README.maildirfilter.html \
	README.sharedfolders.html README.sharedfolders.txt \
	testsuite testsuite.txt testsuite2 testsuite2.txt quotawarnmsg $(DOCS)

README.maildirquota.txt: README.maildirquota.html
	elinks -dump -no-numbering README.maildirquota.html >README.maildirquota.txt

README.sharedfolders.txt: README.sharedfolders.html
	elinks -dump -no-numbering README.sharedfolders.html >README.sharedfolders.txt

mailbot.h: config.status
	echo '#define MAILBOT "@MAILBOT@"' >mailbot.h

maildirsharedrc.h: config.status
	echo '#define MAILDIRSHAREDRC "$(sysconfdir)/maildirshared"' >maildirsharedrc.h

maildirfilterconfig.h: config.status
	echo '#define MAILDIRFILTERCONFIG "$(sysconfdir)/maildirfilterconfig"' >maildirfilterconfig.h

autoresponsequota.h: config.status
	echo '#define AUTORESPONSEQUOTA "$(sysconfdir)/autoresponsesquota"' >autoresponsequota.h

quotawarnmsg.h: config.status
	echo '#define QUOTAWARNMSG "$(sysconfdir)/quotawarnmsg"' >quotawarnmsg.h

clean-local:
	rm -rf testmd
	rm -rf $(DEPDIR)

check-am:
	unset VALGRIND; @SHELL@ $(srcdir)/testsuite 2>&1 | diff -U 3 $(srcdir)/testsuite.txt -
	test "@PCRE_LDFLAGS@" != "" || exit 0 ; @SHELL@ $(srcdir)/testsuite2 2>&1 | diff -U 3 $(srcdir)/testsuite2.txt -
	LC_ALL=C; export LC_ALL; ./maildirkwtest
	LC_ALL=C; export LC_ALL; ./maildiraclttest
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		LC_ALL=C; export LC_ALL; valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./maildiraclttest
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		LC_ALL=C; export LC_ALL; valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./maildirkwtest
	./testmaildirsearch iso-8859-1 needle haystack; test $$? = 2 && exit 0; exit 1
	./testmaildirsearch iso-8859-1 nEEdle needlE
	./testmaildirsearch iso-8859-1 "abra  cadAbra" "abra   cadabra"
	./testmaildirsearch iso-8859-1 "  abra  cadAbra  " " abra   cadabra"
	./testmaildirsearch iso-8859-1 "  abra  cadAbra  " " abra   cadabra  "
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		LC_ALL=C; export LC_ALL; export VALGRIND="@SHELL@ $(srcdir)/testsuitevalgrind"; rm -f confmdtesterror; @SHELL@ $(srcdir)/testsuite 2>&1 | tee testsuite.out; test ! -f confmdtesterror
	rm -rf testsuite.out
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		LC_ALL=C; export LC_ALL; valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testautoresponse
	./testmaildirmisc
	VALGRIND="`which valgrind 2>/dev/null`"; test "$$VALGRIND" != "" || exit 0; \
		LC_ALL=C; export LC_ALL; valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./testmaildirmisc

if HAVE_SGML

deliverquota.html.in: deliverquota.sgml ../docbook/sgml2html
	../docbook/sgml2html deliverquota.sgml deliverquota.html.in

deliverquota.8.in: deliverquota.sgml ../docbook/sgml2html
	../docbook/sgml2man deliverquota.sgml deliverquota.8.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv deliverquota.8 deliverquota.8.in

maildirmake.html.in: maildirmake.sgml ../docbook/sgml2html
	../docbook/sgml2html maildirmake.sgml maildirmake.html.in

maildirmake.1.in: maildirmake.sgml ../docbook/sgml2html
	../docbook/sgml2man maildirmake.sgml maildirmake.1.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv maildirmake.1 maildirmake.1.in

maildiracl.html.in: maildiracl.sgml ../docbook/sgml2html
	../docbook/sgml2html maildiracl.sgml maildiracl.html.in

maildiracl.1.in: maildiracl.sgml ../docbook/sgml2html
	../docbook/sgml2man maildiracl.sgml maildiracl.1.in "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"
	mv maildiracl.1 maildiracl.1.in

maildirquota.html: maildirquota.sgml ../docbook/sgml2html
	../docbook/sgml2html maildirquota.sgml maildirquota.html

maildirquota.7: maildirquota.sgml ../docbook/sgml2man
	../docbook/sgml2man maildirquota.sgml maildirquota.7 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"

maildir.html: maildir.sgml ../docbook/sgml2html
	../docbook/sgml2html maildir.sgml maildir.html

maildir.5: maildir.sgml ../docbook/sgml2man
	../docbook/sgml2man maildir.sgml maildir.5 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"

maildirkw.html: maildirkw.sgml ../docbook/sgml2html
	../docbook/sgml2html maildirkw.sgml maildirkw.html

maildirkw.1: maildirkw.sgml ../docbook/sgml2man
	../docbook/sgml2man maildirkw.sgml maildirkw.1 "--stringparam man.base.url.for.relative.links http://www.courier-mta.org/"

maildirwatch.html: maildirwatch.sgml ../docbook/sgml2html
	../docbook/sgml2html maildirwatch.sgml maildirwatch.html

maildirwatch.1: maildirwatch.sgml ../docbook/sgml2man
	../docbook/sgml2man maildirwatch.sgml maildirwatch.1

endif

deliverquota.html: deliverquota.html.in
	./config.status --file=$@

deliverquota.8: deliverquota.8.in
	./config.status --file=$@

maildirmake.html: maildirmake.html.in
	./config.status --file=$@

maildirmake.1: maildirmake.1.in
	./config.status --file=$@

maildiracl.html: maildiracl.html.in
	./config.status --file=$@

maildiracl.1: maildiracl.1.in
	./config.status --file=$@

# autoresponse.c: autoresponsequota.h

DISTCLEANFILES=$(BUILT_SOURCES)
