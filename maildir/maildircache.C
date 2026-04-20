/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


#include	"config.h"
#include	"maildircache.h"
#include	"numlib/numlib.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<signal.h>
#include	<pwd.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#include	<sys/types.h>
#if	HAVE_SYS_STAT_H
#include	<sys/stat.h>
#endif
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if	HAVE_FCNTL_H
#include	<fcntl.h>
#endif

#include	<iostream>
#include	<fstream>
#include	<filesystem>
#include	<charconv>

#define exit(_a_) _exit(_a_)

#include <string>
#include <vector>

static std::vector<std::string> authvars;
static std::vector<std::string> authvals;
static time_t expinterval;
static time_t lastclean=0;
static const char *cachedir;
static const char *cacheowner;

void maildir_cache_init(time_t n, const char *d, const char *o,
			const char * const *a)
{
	size_t x=0;

	expinterval=n;
	cachedir=d;
	cacheowner=o;

	if (a)
		for (x=0; a[x]; x++)
			;
	authvars.reserve(x);
	authvals.resize(x);

	for (x=0; a[x]; x++)
		authvars.push_back(a[x]);
}


static std::string create_cache_name(const char *userid, time_t login_time)
{
	size_t	l;
	char	buf[NUMBUFSIZE];

	login_time /= expinterval;
	l=0;
	for (const char *p=userid; *p; p++)
	{
		++l;
		if ((unsigned char)*p < ' ' ||
		    *p == ';' || *p == '\'' || *p == ';')
		{
			std::cerr << "CRIT: maildircache: invalid chars in"
					  " userid: " << p << "\n";
			return (NULL);
		}
		if (*p == '/' || *p == '+' || (int)(unsigned char)*p >= 127)
			l += 2;
	}
	std::string g;
	g.reserve(l);

	while (*userid)
	{
		if (*userid == '/' || *userid == '+'
			|| (int)(unsigned char)*userid >= 127)
		{
		static char xdigit[]="0123456789ABCDEF";

			g.push_back('+');
			g.push_back(xdigit[ (*userid >> 4) & 15 ]);
			g.push_back(xdigit[ (*userid) & 15 ]);
		}
		else
			g.push_back(*userid);

		++userid;
	}

	l=sizeof("//xx/xxxxxxx") + strlen(cachedir);
	l += strlen(libmail_str_time_t( login_time, buf)) + g.size();

	std::string f;
	f.reserve(l);
	f += cachedir;
	f += "/";
	f += buf;
	f += "/";
	strncpy(buf, g.c_str(), 2);
	buf[2]=0;
	while (strlen(buf) < 2)	strcat(buf, "+");
	f += buf;
	f += "/";
	f += g;
	return (f);
}

static pid_t childproc= -1;
static int childpipe;

void maildir_cache_start()
{
	int	pipefd[2];
	char	buf[2048];
	size_t i;
	int	j;
	char	*userid, *login_time, *data;
	time_t	login_time_n;
	std::ofstream fp;

	if (pipe(pipefd) < 0)
	{
		perror("CRIT: maildircache: pipe() failed");
		return;
	}
	while ((childproc=fork()) < 0)
	{
		sleep(5);
	}

	if (childproc)
	{
		close(pipefd[0]);
		childpipe=pipefd[1];
		return;
	}
	close(pipefd[1]);
	i=0;

	for (;;)
	{
		if (i >= sizeof(buf)-1)
		{
			close(pipefd[0]);

			/* Problems */

			std::cerr << "CRIT: maildircache: Max cache"
					  " buffer overflow.\n";
			exit(1);
		}

		j=read(pipefd[0], buf+i, sizeof(buf)-1-i);
		if (j < 0)
		{
			perror("CRIT: maildircache: Cache create failure");
			exit(1);
		}
		if (j == 0)	break;
		i += j;
	}
	close(pipefd[0]);
	buf[i]=0;

	{
	struct passwd *pwd=getpwnam(cacheowner);

		if (!pwd || setgid(pwd->pw_gid) || setuid(pwd->pw_uid))
		{
			std::cerr << "CRIT: maildircache: Cache create failure"
					  " - cannot change to user "
					  << cacheowner << "\n";
			exit(1);
		}
	}

	if (strncmp(buf, "CANCELLED\n", 10) == 0)
		exit (0);

	userid=buf;
	if ((login_time=strchr(userid, ' ')) == 0)
	{
		std::cerr << "CRIT: maildircache: Cache create failure"
				  " - authentication process crashed.\n";
		exit(1);
	}
	*login_time++=0;
	if ((data=strchr(login_time, ' ')) == 0)
	{
		std::cerr << "CRIT: maildircache: Cache create failure" 
				  " - authentication process crashed.\n";
		exit(1);
	}
	*data++=0;

	login_time_n=0;
	while (*login_time >= '0' && *login_time <= '9')
		login_time_n = login_time_n * 10 + (*login_time++ -'0');

	std::string f=create_cache_name(userid, login_time_n);

	fp.open(f.c_str());
	if (!fp)	/* Try creating subdirs */
	{
		size_t n=strlen(cachedir);

		while (n < f.size() && f[n] == '/')
		{
			f[n]=0;
			mkdir(f.c_str(), 0700);
			f[n]='/';
			n=f.find('/', n+1);
		}

		fp.open(f.c_str());
		if (!fp)
		{
			std::cerr << "CRIT: maildircache: Cache create failure" 
					  " - unable to create file "
					  << f << "\n";
			exit(1);
		}
	}

	fp.write(data, strlen(data));
	if (fp.fail())
	{
		fp.close();
		unlink(f.c_str());	/* Problems */
		std::cerr << "CRIT: maildircache: Cache create failure" 
				  " - write error.\n";
		exit(1);
	}
	else	fp.close();
	exit(0);
}

static int savebuf(std::string_view p)
{
	while (p.size())
	{
	int	n=write(childpipe, p.data(), p.size());

		if (n <= 0)	return (-1);
		p = p.substr(n);
	}
	return (0);
}

void maildir_cache_save(const char *a, time_t b, const char *homedir,
		      uid_t u, gid_t g)
{
	std::string buf;
	char	buf2[NUMBUFSIZE];
	pid_t	p;
	int	waitstat;

	buf += a;
	buf += " ";
	buf += libmail_str_time_t(b, buf2);
	buf += " ";
	buf += libmail_str_uid_t(u, buf2);
	buf += " ";
	buf += libmail_str_gid_t(g, buf2);
	buf += " ";
	buf += homedir;
	buf += "\n";

	if (savebuf(buf) == 0)
	{
		for (auto &v:authvars)
		{
		const char *p;

			buf.clear();
			buf += v;
			buf += "=";
			p=getenv(v.c_str());
			if (strchr(p, '\n'))
				continue;
			buf += p;
			buf += "\n";
			if (savebuf(buf))	break;
		}
	}
	close(childpipe);
	while ((p=wait(&waitstat)) != -1 && p != childproc)
		;
	childproc= -1;
}

void maildir_cache_cancel()
{
	if (childproc > 0)
	{
		if (write(childpipe, "CANCELLED\n", 10) < 0)
			perror("write");

		close(childpipe);
	}
}

int maildir_cache_search(const char *a, time_t b,
			 int (*callback_func)(uid_t, gid_t, const char *,
					      void *), void *callback_arg)
{
	std::string f=create_cache_name(a, b);
	std::ifstream fp;
	uid_t	u;
	gid_t	g;
	size_t	n;
	int	c;

	fp.open(f);
	if (!fp)
		return (-1);

	u=0;
	while ((c=fp.get()) != ' ')
	{
		if (c < '0' || c > '9')
			return (-1);
		u=u*10 + (c-'0');
	}

	g=0;
	while ((c=fp.get()) != ' ')
	{
		if (c < '0' || c > '9')
			return (-1);
		g=g*10 + (c-'0');
	}

	std::string dir;
	while ((c=fp.get()) != EOF)
	{
		if (c == '\n')	break;
		dir.push_back(c);
	}

	if ((n=(*callback_func)(u, g, dir.c_str(), callback_arg)) != 0)
		return (n);

	if (c != EOF)
	{
		while (std::getline(fp, dir))
		{
			for (size_t i=0; i<authvars.size(); i++)
			{
				size_t l=authvars[i].size();

				if (dir.size() <= l)
					continue;
				if (strncmp(dir.data(), authvars[i].c_str(), l) == 0 &&
				    dir[l] == '=')
				{
					authvals[i]=dir;
					putenv(authvals[i].data());

					break;
				}
			}
		}
	}
	return (0);
}

void maildir_cache_purge(time_t now)
{
	pid_t p;
	int waitstat;
	struct passwd *pw;
	std::vector<std::string> pl;
	struct sigaction sa, oldsa;

	if (lastclean && lastclean >= now - expinterval)
		return;

	lastclean=now;

	memset(&sa, 0, sizeof(sa));

	sa.sa_handler=SIG_DFL;
	if (sigaction(SIGCHLD, &sa, &oldsa) < 0)
	{
		perror("sigaction");
		return;
	}

	p=fork();

	if (p < 0)
		return;

	if (p)
	{
		pid_t p2;

		while ((p2=wait(&waitstat)) >= 0 && p2 != p)
			;

		sigaction(SIGCHLD, &oldsa, NULL);
		return;
	}

#ifndef UNIT_TEST
	p=fork();

	if (p)
		exit(0);
#endif

	pw=getpwnam(cacheowner);

	if (!pw)
	{
		std::cerr << "CRIT: maildircache: no such user "
				  << cacheowner
				  << " - cannot purge login cache dir\n";
		exit(0);
	}

	if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
	{
		std::cerr << "CRIT: maildircache: cannot change to uid/gid for "
				  << cacheowner
				  << " - cannot purge login cache dir\n";
		exit(0);
	}

	if (chdir(cachedir))
	{
		std::cerr << "CRIT: maildircache: cannot change dir to "
				  << cachedir << "\n";
		exit(0);
	}

	now /= expinterval;
	--now;

	for (auto &de:std::filesystem::directory_iterator("."))
	{
		std::string n=de.path().filename();
		if (n[0] < '0' || n[0] > '9')
			continue;

		time_t timestamp;

		if (std::from_chars(n.data(), n.data()+n.size(), timestamp).ec
			!= std::errc{})
			continue;

		if (timestamp >= now)
			continue;

		pl.push_back(n);
	}

	for (auto &p:pl)
	{
		std::filesystem::remove_all(p);
	}
	exit(0);
}
