#
# Copyright 1998 - 2005 S. Varshavchik.  See COPYING for
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
	maildircache.c maildircache.h \
	maildircreate.C maildircreate.h \
	maildirfilename.c maildirgetnew.c \
	maildirfilter.c maildirfilter2.c maildirfiltercpp.C \
	maildirfilter.h maildirfiltertypelist.h\
	maildirflags.c maildirmkdir.c \
	maildirgetquota.c maildirgetquota.h \
	maildirinfo.c maildirinfo.h maildirinfo2.C \
	maildirinfocpp.C \
	maildirkeywords.c maildirkeywords2.c maildirkeywords2cpp.C \
	maildirkeywords3.c \
	maildirkeywords4.cpp \
	maildirkeywords.h maildirlist.C maildirlock.c \
	maildirmake2.c \
	maildirnewshared.c maildirnewshared.h maildirnewshared2.c \
	maildiropen.c maildirparsequota.c \
	maildirpathcpp.C maildirpurgetmp.c maildirmisc.h \
	maildirrename.c \
	maildirsearch.c maildirsearchC.cpp maildirsearch.h \
	maildirshared.c maildirsharedcpp.C maildirshared2.C maildirdelfolder.c\
	maildirquota.c maildirquota.h maildirrequota.c maildirrequota.h \
	maildirwatch.C maildirwatch.h loginexec.c loginexec.h

noinst_PROGRAMS=deliverquota maildirmake testmaildirfilter maildirkwtest \
	maildirkwtest2 testautoresponse \
	maildirkw maildiracl maildiraclttest testmaildirsearch maildirwatch

deliverquota_SOURCES=deliverquota.c
deliverquota_DEPENDENCIES=libmaildir.la ../rfc822/librfc822.la \
				../numlib/libnumlib.la
deliverquota_LDADD=libmaildir.la ../rfc822/librfc822.la ../numlib/libnumlib.la
deliverquota_LDFLAGS=-static
nodist_EXTRA_deliverquota_SOURCES=dummy.C

maildirmake_SOURCES=maildirmake.c
maildirmake_DEPENDENCIES=libmaildir.la \
			../numlib/libnumlib.la \
			../rfc822/librfc822.la
maildirmake_LDADD=libmaildir.la \
			../numlib/libnumlib.la \
			../rfc822/librfc822.la -lcourier-unicode @PCRE_LDFLAGS@
maildirmake_LDFLAGS=-static
nodist_EXTRA_maildirmake_SOURCES=dummy.C

testmaildirfilter_SOURCES=maildirfiltertypelist.h testmaildirfilter.c
testmaildirfilter_DEPENDENCIES=libmaildir.la ../numlib/libnumlib.la
testmaildirfilter_LDADD=libmaildir.la ../numlib/libnumlib.la -lcourier-unicode  @PCRE_LDFLAGS@
testmaildirfilter_LDFLAGS=-static
nodist_EXTRA_testmaildirfilter_SOURCES=dummy.C

maildirkwtest_SOURCES=maildirkwtest.C
maildirkwtest_LDADD=libmaildir.la
maildirkwtest_DEPENDENCIES=libmaildir.la
maildirkwtest_LDFLAGS=-static
nodist_EXTRA_maildirkwtest_SOURCES=dummy.C

maildirkw_SOURCES=maildirkw.C
maildirkw_LDADD=libmaildir.la ../liblock/liblock.la ../numlib/libnumlib.la
maildirkw_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
	../numlib/libnumlib.la
maildirkw_LDFLAGS=-static
nodist_EXTRA_maildirkw_SOURCES=dummy.C

maildirkwtest2_SOURCES=maildirkw.C maildirkwtest2.c
maildirkwtest2_LDADD=$(maildirkw_LDADD)
maildirkwtest2_DEPENDENCIES=$(maildirkw_DEPENDENCIES)
maildirkwtest2_LDFLAGS=-static

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

maildiraclttest_SOURCES=testmaildiraclt.c
maildiraclttest_LDADD=libmaildir.la ../liblock/liblock.la \
		../numlib/libnumlib.la
maildiraclttest_DEPENDENCIES=libmaildir.la ../liblock/liblock.la \
		../numlib/libnumlib.la
maildiraclttest_LDFLAGS=-static
nodist_EXTRA_maildiraclttest_SOURCES=dummy.C

testmaildirsearch_SOURCES=testmaildirsearch.c
testmaildirsearch_LDADD=libmaildir.la -lcourier-unicode
testmaildirsearch_DEPENDENCIES=libmaildir.la

testautoresponse_SOURCES=testautoresponse.C
testautoresponse_LDADD=libmaildir.la -lcourier-unicode
testautoresponse_DEPENDENCIES=libmaildir.la

EXTRA_DIST=README.maildirquota.html README.maildirquota.txt \
	testsuitevalgrind \
	README.imapkeywords.html \
	README.maildirfilter.html \
	README.sharedfolders.html README.sharedfolders.txt \
	maildirkwtest.txt \
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
	LC_ALL=C; export LC_ALL; ./maildirkwtest | cmp -s - $(srcdir)/maildirkwtest.txt
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
