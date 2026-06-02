/*
** Copyright 2000 S. Varshavchik.
** See COPYING for distribution information.
*/

/*
*/

#include	"config.h"

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>

#if	HAVE_STRINGS_H
#include	<strings.h>
#endif

#if	HAVE_LOCALE_H
#include	<locale.h>
#endif

#define private public
#include	"imaprefs.h"
#undef private
#include <set>
#include <string_view>

static void test1()
{
	rfc822::refmsgtable mt;
	char buf[20];

        strcpy(buf, "a@b");
        mt.threadallocmsg(buf);
        strcpy(buf, "c@d");
        mt.threadallocmsg(buf);

	printf("%s\n", (mt.threadsearchmsg("a@b")
			? "found":"not found"));
	printf("%s\n", (mt.threadsearchmsg("c@d")
			? "found":"not found"));
	printf("%s\n", (mt.threadsearchmsg("e@f")
			? "found":"not found"));
}

static void prtree(rfc822::refmsgtable::refmsg *m)
{
	printf("<%s>", m->msgid ? m->msgid:"");

	if (m->isdummy)
	{
		printf(" (dummy)");
	}

	printf(".parent=");
	if (m->parent)
		printf("<%s>", m->parent->msgid ? m->parent->msgid:"");
	else
		printf("ROOT");

	printf("\n");

	for (m=m->firstchild; m; m=m->nextsib)
		prtree(m);
}

static void prpc(rfc822::refmsgtable *mt)
{
	rfc822::refmsgtable::refmsg *root=mt->threadgetroot(), *m;

	if (!root)
		return;

	for (m=root->firstchild; m; m=m->nextsib)
		prtree(m);

	printf("\n\n");
}

static void test2()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<1>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<2>",
			 "<1>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<4>",
			 "<1> <2> <3>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	prpc(&mt);
}

static void test3()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<4>",
			 "<2> <1> <3>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<3>",
			 "<1> <2>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<2>",
			 "<1>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<1>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	prpc(&mt);
}

static void test4()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<1>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<2>", "<1>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<4>", "<1> <2> <3>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	prpc(&mt);
	mt.threadprune();
	prpc(&mt);
}

static void test5()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<4>", "<1> <2> <3>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<3>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	prpc(&mt);
	mt.threadprune();
	prpc(&mt);
}

static void prsubj(rfc822::refmsgtable *p)
{
	std::set<std::string_view> names;

	for (auto &[name, info] : p->subjtable)
		names.insert(name);

	for (auto &name:names)
	{
		auto info=p->subjtable.find(name);
		auto &[isrefwd, msg] = info->second;
		printf("subject(%s)=<%s>\n", name.data(),
		       msg ? msg->msgid:"");
	}
	printf("\n\n");
}

static void test6()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<message1>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<message10>", NULL,
			 "subject 2",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 2);

	mt.threadmsg("<message3>", "<message2>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 3);

	mt.threadmsg("<message11>", NULL,
			 "Re: subject 4",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 4);

	mt.threadmsg("<message12>", NULL,
			 "subject 4",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 5);

	mt.threadmsg("<message13>", NULL,
			 "subject 5",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 6);

	mt.threadmsg("<message14>", NULL,
			 "re: subject 5",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 7);

	mt.threadprune();
	mt.threadsortsubj(mt.threadgetroot());
	mt.threadgathersubj(mt.threadgetroot());
	prpc(&mt);
	prsubj(&mt);
}

static void test7()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<message1>", "<message1-dummy>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<message2>", "<message2-dummy>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:58 -0700", 0, 1);
	mt.threadprune();
	mt.threadsortsubj(mt.threadgetroot());
	mt.threadgathersubj(mt.threadgetroot());
	prpc(&mt);
	prsubj(&mt);
	mt.threadmergesubj(mt.threadgetroot());
	prpc(&mt);
}

static void test8()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<message4>", NULL,
			 "subject 2",
			 "Thu, 29 Jun 2000 14:41:51 -0700", 0, 1);

	mt.threadmsg("<message2>", NULL,
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:52 -0700", 0, 1);

	mt.threadmsg("<message1>", "<message1-dummy>",
			 "subject 1",
			 "Thu, 29 Jun 2000 14:41:53 -0700", 0, 1);

	mt.threadmsg("<message3>", NULL,
			 "Re: subject 2",
			 "Thu, 29 Jun 2000 14:41:54 -0700", 0, 1);

	mt.threadmsg("<message10>", NULL,
			 "subject 10",
			 "Thu, 29 Jun 2000 14:41:55 -0700", 0, 1);

	mt.threadmsg("<message11>", NULL,
			 "subject 10",
			 "Thu, 29 Jun 2000 14:41:56 -0700", 0, 1);

	mt.threadprune();
	mt.threadsortsubj(mt.threadgetroot());
	mt.threadgathersubj(mt.threadgetroot());
	prpc(&mt);
	prsubj(&mt);
	mt.threadmergesubj(mt.threadgetroot());
	prpc(&mt);
}

static void test9()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<message1>", NULL,
			 "subject 1",
			 "Thu, 20 Jun 2000 14:41:55 -0700", 0, 1);

	mt.threadmsg("<message2>", NULL,
			 "subject 1",
			 "Thu, 19 Jun 2000 14:41:51 -0700", 0, 2);

	mt.threadmsg("<message3>", NULL,
			 "subject 1",
			 "Thu, 21 Jun 2000 14:41:56 -0700", 0, 3);

	mt.threadmsg("<message4>", "<message2>",
			 "subject 2",
			 "Thu, 21 Jun 2000 14:41:54 -0700", 0, 6);

	mt.threadmsg("<message5>", "<message2>",
			 "subject 2",
			 "Thu, 21 Jun 2000 14:41:53 -0700", 0, 5);

	mt.threadmsg("<message6>", "<message2>",
			 "subject 2",
			 "Thu, 20 Jun 2000 14:41:52 -0700", 0, 4);


	mt.threadprune();
	mt.threadsortsubj(mt.threadgetroot());
	mt.threadgathersubj(mt.threadgetroot());
	mt.threadmergesubj(mt.threadgetroot());
	mt.threadsortbydate();
	prpc(&mt);
}

static void test10()
{
	rfc822::refmsgtable mt;

	mt.threadmsg("<message1>", NULL,
			 "subject 1",
			 "Thu, 20 Jun 2000 14:41:58 -0700", 0, 1);

	mt.threadmsg("<message4>", "<message1>",
			 "subject 2",
			 "Thu, 21 Jun 2000 14:41:58 -0700", 0, 6);

	mt.threadmsg("<message1>", NULL,
			 "subject 2",
			 "Thu, 21 Jun 2000 14:41:58 -0700", 0, 5);

	mt.threadmsg("<message4>", "<message1>",
			 "subject 2",
			 "Thu, 21 Jun 2000 14:41:58 -0700", 0, 6);

	mt.threadprune();
	mt.threadsortsubj(mt.threadgetroot());
	mt.threadgathersubj(mt.threadgetroot());
	mt.threadmergesubj(mt.threadgetroot());
	mt.threadsortbydate();
	prpc(&mt);
}

int main(int argc, char **argv)
{

#if HAVE_SETLOCALE
	setlocale(LC_ALL, "C");
#endif

	test1();
	test2();
	test3();
	test4();
	test5();
	test6();
	test7();
	test8();
	test9();
	test10();
	return (0);
}
