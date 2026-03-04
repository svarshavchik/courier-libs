/*
** Copyright 1998 - 2006 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"ispell.h"
#include	<stdlib.h>
#include	"rfc822/rfc822.h"
#include	<unordered_set>
#include	<iterator>
#include	<algorithm>

#if	HAVE_CONFIG_H
#include	<config.h>
#endif

#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif

#include	<sys/types.h>
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif

#include	<signal.h>
#include	<string.h>

static pid_t ispell_pid;
static void (*prevsighandler)(int);

#ifndef	ISPELL
#define	ISPELL	"/usr/bin/ispell"
#endif

#ifndef SPELLPROG
#define SPELLPROG ISPELL
#endif

static void wait4pid()
{
int	waitstat;

	if (prevsighandler == SIG_DFL)
		while ( wait(&waitstat) != ispell_pid)
			;
}

static int fork_ispell(std::string_view ispellline, const char *dict,
		       std::string &response)
{
int	fdbuf[2], fdbuf2[2];
int	waitstat;
const char *args[6];
int	argn;

	response.clear();

	prevsighandler=signal(SIGCHLD, SIG_DFL);
	signal(SIGCHLD, prevsighandler);

	if (pipe(fdbuf) < 0)
	{
		perror("pipe");
		return (-1);
	}

	switch (ispell_pid=fork())	{
	case -1:
		perror("fork");
		close(fdbuf[0]);
		close(fdbuf[1]);
		return (-1);
	case 0:
		if (pipe(fdbuf2) < 0) {
			perror("pipe");
			_exit(-1);
		}

		/*
		** First child will fork and run the real ispell, and write to
		** it on stdin.
		*/

		switch (ispell_pid=fork())	{
		case -1:
			perror("fork");
			_exit(-1);
		case 0:
			dup2(fdbuf2[0], 0);
			dup2(fdbuf[1], 1);
			close(fdbuf[0]);
			close(fdbuf[1]);
			close(fdbuf2[0]);
			close(fdbuf2[1]);
			argn=0;
			args[argn++]=SPELLPROG;
			args[argn++]="-a";
			if (dict)
			{
				args[argn++]="-d";
				args[argn++]=dict;
			}
			args[argn]=0;
			execv(SPELLPROG, (char **)args);
			perror("fork_ispell: execv " SPELLPROG);
			_exit(1);
		}
		close(fdbuf[0]);
		close(fdbuf[1]);
		close(fdbuf2[0]);
		signal(SIGCHLD, SIG_DFL);
		auto l=ispellline.size();
		auto p=ispellline.data();
		while (l)
		{
			int	n=write(fdbuf2[1], p, l);

			if (n <= 0)
			{
				perror("fork_ispell: write");
				_exit(1);
			}
			p += n;
			l -= n;
		}
		if (write(fdbuf2[1], "\n", 1) != 1)
		{
			perror("fork_ispell: write");
			_exit(1);
		}
		close(fdbuf2[1]);
		while ( wait(&waitstat) != ispell_pid)
			;
		_exit(0);
	}

	close(fdbuf[1]);
	rfc822::fdstreambuf resp{fdbuf[0]};

	response.insert(response.end(),
			std::istreambuf_iterator<char>{&resp},
			std::istreambuf_iterator<char>{}
	);
	wait4pid();
	return (0);
}

ispell::ispell(const char *dict, std::string_view line)
{
	if (fork_ispell(line, dict, buffer))
	{
		fprintf(stderr, "ERR: ispell_run: fork_ispell failed\n");
		return;
	}

	auto p=buffer.data();
	auto e=p+buffer.size();

	if ((p=std::find(p, e, '\n')) == e)
	{
		fprintf(stderr, "ERR: ispell_run: incomplete result from ispell\n");
		return;
	}

	for (char *q; p != e && ++p != e; p=q)
	{
		// & misspelledworld 2 6: world, w world

		q=std::find(p, e, '\n');

		if (p == q) continue;
		if ( *p != '&' && *p != '?' && *p != '#')	continue;

		auto c=*p++;
		while (p != q && *p == ' ') ++p;

		auto start=p;

		while (p != q && *p != ' ') ++p;

		std::string_view misspelled_word{
			start,
			static_cast<size_t>(p-start)
		};

		size_t nmisses=0;
		while (p != q && *p == ' ')	++p;
		if (c == '&' || c == '?')
		{
			while (p != q && *p >= '0' && *p <= '9')
				nmisses=nmisses * 10 + *p++ - '0';
			while (p != q && *p == ' ')	++p;
		}

		size_t word_pos=0;
		while (p != q && *p >= '0' && *p <= '9')
			word_pos=word_pos * 10 + *p++ - '0';

		misspelled_words.push_back({misspelled_word, word_pos});
		auto &misspelling=misspelled_words.back();

		if (nmisses == 0 || p == q || *p != ':')	continue;
		++p;

		std::unordered_set<std::string_view> dupes;

		while (p != q)
		{
			if (*p == ' ' || *p == ',')
			{
				++p;
				continue;
			}

			auto start=p;

			p=std::find_if(
				p, q, [](char c)
				{
					return c == ' ' || c == ',';
				});

			std::string_view suggestion{
				start,
				static_cast<size_t>(p-start)
			};

			if (!dupes.insert(suggestion).second)
				continue;

			misspelling.suggestions.push_back(suggestion);
		}
	}
}

ispell::~ispell()=default;
