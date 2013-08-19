#
# Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
# distribution information.

@SET_MAKE@

SUBDIRS=@LANGUAGES@

htmllibdir=@htmldir@

DISTCLEANFILES=README_LANG.txt

EXTRA_DIST=mksoftlinks rmsoftlinks lstsoftlinks README_LANG.txt

install-data-hook:
	@for lang in `cat ${srcdir}/*/LANGUAGE_PREF | sort | awk ' { print $$2 } ' | sort | uniq` ; do ${srcdir}/mksoftlinks $(DESTDIR)$(htmllibdir) $$lang ; done

uninstall-local:
	@for lang in `cat ${srcdir}/*/LANGUAGE_PREF | sort | awk ' { print $$2 } ' | sort | uniq` ; do ${srcdir}/rmsoftlinks $(DESTDIR)$(htmllibdir) $$lang ; done

install-perms:
	@$(MAKE) -$(MAKEFLAGS) install-perms-recursive
	@$(MAKE) -$(MAKEFLAGS) install-perms-local

install-perms-recursive:
	@>permissions.dat
	@SUBDIRS="$(SUBDIRS)" ; for f in $$SUBDIRS ; do ( cd $$f && $(MAKE) $(AM_MAKEFLAGS) install-perms ) ; cat /dev/null >>$$f/permissions.dat ; cat $$f/permissions.dat >>permissions.dat ; done

install-perms-local:
	@${srcdir}/lstsoftlinks `cat ${srcdir}/*/LANGUAGE_PREF | sort | awk ' { print $$2 } ' | sort | uniq` | sort | uniq | while read F ; \
	do echo "$(htmllibdir)/$$F 644 bin bin" ; done >>permissions.dat
	@echo "$(htmllibdir) 555 bin bin"  >>permissions.dat

clone:
	@test -d ${srcdir}/${to} || exit 0; echo ${srcdir}/${to} already exists. ; exit 1
	cd ${from} && $(MAKE) clonedist
	mkdir ${srcdir}/${to}
	d=`pwd`; cd ${srcdir}/${to} && tar xzf $$d/${from}/clone.tar.gz
	echo ${to} >${srcdir}/${to}/LANGUAGE
	cd ${srcdir}/${to} ; pref=`awk ' { print $$1 }' <LANGUAGE_PREF` ; echo $$pref ${to} >LANGUAGE_PREF

HTML2TXT=`which lynx 2>/dev/null && echo "lynx -dump -nolist" && exit; echo "links -dump"`

README_LANG.txt: README_LANG.html
	$(HTML2TXT) README_LANG.html >README_LANG.txt
