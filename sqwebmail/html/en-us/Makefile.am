
@SET_MAKE@

htmllibdir=@htmldir@/@lang@
conf=LOCALE LANGUAGE LANGUAGE_PREF ISPELLDICT CHARSET TIMEZONELIST smileys.txt
html=abooklist.html acl.html attachments.html autoresponder.html \
	     empty.html eventacl.html eventdaily.html \
	     eventdelete.html eventmonthly.html \
	     eventshow.html eventweekly.html expired.html folder.html \
             folders.html index.html invalid.html keyimport.html login.html \
	     filter.html ldaplist.html ldapsearch.html \
             newevent.html newmsg.html preferences.html printnocookie.html \
	     printredirect.html quickadd.html print.html readmsg.html \
	     redirect.html refreshfr.html spellchk.html \
	     gpg.html gpgcreate.html gpgerr.html \
	     loginform.inc.html \
	     navbar.inc.html navbar2.inc.html navbar3.inc.html \
	     calendarlogin.inc.html \
	     eventnotifynew.txt eventnotifydelete.txt eventnotifysubject.txt

htmllib_DATA=$(conf) $(html)

EXTRA_DIST=$(htmllib_DATA)

install-data-hook:
	test -f ../../TIMEZONELIST || exit 0; \
		@SED@ -n '/^\#/p' < $(srcdir)/TIMEZONELIST \
			> $(DESTDIR)$(htmllibdir)/TIMEZONELIST ; \
		cat ../../TIMEZONELIST >>$(DESTDIR)$(htmllibdir)/TIMEZONELIST

clonedist: distdir
	-chmod -R a+r $(distdir)
	d=`pwd` ; cd $(distdir) ; GZIP=$(GZIP_ENV) $(AMTAR) chozf $$d/clone.tar.gz .
	-rm -rf $(distdir)

install-perms:
	@echo "$(htmllibdir) 555 bin bin" >permissions.dat
	@for f in $(conf) ; do\
		echo $(htmllibdir)/$$f 644 bin bin config >>permissions.dat ;\
		done
	@for f in $(html) ; do\
		echo $(htmllibdir)/$$f 644 bin bin >>permissions.dat ;\
		done

