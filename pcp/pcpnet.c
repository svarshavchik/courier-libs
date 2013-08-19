/*
** Copyright 2001-2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <rfc822/rfc822hdr.h>
#include "pcp.h"
#include "calendardir.h"

#if HAVE_DIRENT_H
#include <dirent.h>
#else
#define dirent direct
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif

#define HOSTNAMELEN 256

#define EVENTID_MAXLEN 512
#define ADDR_MAXLEN 512

#define EVENTID_SSCANF "%511s"
#define ADDR_SSCANF "%511s"

struct PCPnet {
	struct PCP pcp;
	char *username;
	char *authtoken;
	char *sockname;
	int fd;

	char *readbuf;
	size_t readbuflen;

	char *readptr;
	size_t readleft;

	int haserrmsg;
} ;

struct PCPnet_new_eventid {
	struct PCP_new_eventid eventid;
	int isbooked;
} ;

static void pcp_close_quit_net(struct PCPnet *);
static void pcp_close_net(struct PCPnet *);
static int cleanup(struct PCPnet *);

static struct PCP_new_eventid *neweventid(struct PCPnet *,
					  const char *,
					  struct PCP_save_event *);
static void destroyeventid(struct PCPnet *, struct PCPnet_new_eventid *);

static int commitevent(struct PCPnet *, struct PCPnet_new_eventid *,
		       struct PCP_commit *);
static int bookevent(struct PCPnet *, struct PCPnet_new_eventid *,
		     struct PCP_commit *);

static int listallevents(struct PCPnet *, struct PCP_list_all *);

static int cancelevent(struct PCPnet *, const char *, int *);
static int uncancelevent(struct PCPnet *, const char *,
			 int, struct PCP_uncancel *);
static int deleteevent(struct PCPnet *, struct PCP_delete *);
static int retrevent(struct PCPnet *, struct PCP_retr *);
static int setacl(struct PCPnet *, const char *, int);
static int listacl(struct PCPnet *,
		   int (*)(const char *, int, void *),
		   void *);
static void noop(struct PCPnet *);

static const char *getauthtoken(struct PCPnet *pcp)
{
	return (pcp->authtoken);
}

static const char *errmsg(struct PCPnet *pcp)
{
	if (pcp->haserrmsg)
		return (pcp->readbuf);
	return (strerror(errno));
}

static struct PCPnet *mkpcp(const char *username)
{
	struct PCPnet *pd=(struct PCPnet *)malloc(sizeof(struct PCPnet));
	const char *p;

	if (!pd)
		return (NULL);

	if (!*username)
	{
		free(pd);
		errno=EIO;
		return (NULL);
	}

	for (p=username; *p; p++)
		if (isspace((int)(unsigned char)*p))
		{
			free(pd);
			errno=EIO;
			return (NULL);
		}

	memset(pd, 0, sizeof(*pd));

	pd->fd= -1;
	pd->username=strdup(username);
	if (!pd->username)
	{
		free(pd);
		return (NULL);
	}

	pd->pcp.authtoken_func=(const char *(*)(struct PCP *))getauthtoken;
	pd->pcp.close_func= (void (*)(struct PCP *)) pcp_close_quit_net;
	pd->pcp.cleanup_func= (int (*)(struct PCP *)) cleanup;

	pd->pcp.create_new_eventid_func=
		(struct PCP_new_eventid *(*)(struct PCP *, const char *,
					     struct PCP_save_event *))
		neweventid;

	pd->pcp.destroy_new_eventid_func=
		(void (*)(struct PCP *, struct PCP_new_eventid *))
		destroyeventid;

	pd->pcp.commit_func=
		(int (*)(struct PCP *, struct PCP_new_eventid *,
			 struct PCP_commit *))
		commitevent;

	pd->pcp.book_func=
		(int (*)(struct PCP *, struct PCP_new_eventid *,
			 struct PCP_commit *))
		bookevent;

	pd->pcp.list_all_func=
		(int (*)(struct PCP *, struct PCP_list_all *))
		listallevents;

	pd->pcp.cancel_func=
		(int (*)(struct PCP *, const char *, int *))
		cancelevent;

	pd->pcp.uncancel_func=
		(int (*)(struct PCP *, const char *, int,
			 struct PCP_uncancel *))
		uncancelevent;

	pd->pcp.delete_func=
		(int (*)(struct PCP *, struct PCP_delete *))
		deleteevent;

	pd->pcp.retr_func=
		(int (*)(struct PCP *, struct PCP_retr *))
		retrevent;

	pd->pcp.errmsg_func=
		(const char *(*)(struct PCP *))
		errmsg;

	pd->pcp.noop_func=(void (*)(struct PCP *))noop;

	pd->pcp.acl_func=
		(int (*)(struct PCP *, const char *, int))setacl;
	pd->pcp.listacl_func=
		(int (*)(struct PCP *, int (*)(const char *, int, void *),
			 void *))listacl;

	return (pd);
}

struct sock_list {
	struct sock_list *next;
	char *filename;
} ;

static int cmp_str(const void *a, const void *b)
{
	return (strcmp(*(const char **)a, *(const char **)b));
}

static int dowrite(struct PCPnet *pcp, const char *s, int l)
{
	if (l <= 0)
		l=strlen(s);

	if (pcp->fd < 0)
	{
		errno=ENETDOWN;
		return (-1);
	}

	while (l)
	{
		int n=write(pcp->fd, s, l);

		if (n <= 0)
		{
			errno=ENETDOWN;
			close(pcp->fd);
			pcp->fd= -1;
			return (-1);
		}

		s += n;
		l -= n;
	}
	return (0);
}

static int readch(struct PCPnet *pcp, size_t n)
{
	if (pcp->readleft == 0)
	{
		int l;
		struct timeval tv;
		fd_set fds;

		/* Read the next chunk after the current line :-) */

		if (n + BUFSIZ > pcp->readbuflen)
		{
			size_t nn=n+BUFSIZ;
			char *p= pcp->readbuf ?
				realloc(pcp->readbuf, nn):malloc(nn);
			if (!p)
			{
				close(pcp->fd);
				pcp->fd= -1;
				return (-1);
			}
			pcp->readbuf=p;
			pcp->readbuflen=nn;
		}

		pcp->readptr=pcp->readbuf + n;

		FD_ZERO(&fds);
		FD_SET(pcp->fd, &fds);
		tv.tv_sec=300;
		tv.tv_usec=0;
		if (select(pcp->fd+1, &fds, NULL, NULL, &tv) <= 0)
		{
			errno=ETIMEDOUT;
			close(pcp->fd);
			pcp->fd= -1;
			return (EOF);
		}
		l=read(pcp->fd, pcp->readptr, BUFSIZ);
		if (l <= 0)
		{
			if (l == 0)
				errno=0;
			close(pcp->fd);
			pcp->fd= -1;
			errno=ECONNRESET;
			return (EOF);
		}
		pcp->readleft=l;
	}

	--pcp->readleft;
	return ((int)(unsigned char)*pcp->readptr++);
}

static int getfullreply(struct PCPnet *pcp)
{
	size_t n=0;
	int ch;

	for (;;)
	{
		size_t nn=n;

		while ((ch=readch(pcp, nn)) != EOF)
		{
			if (ch == '\n')
				break;
			pcp->readbuf[nn++]=ch;
		}

		if (ch == EOF)
			return (-1);

		if (nn-n >= 4 &&
		    isdigit((int)(unsigned char)pcp->readbuf[n]) &&
		    isdigit((int)(unsigned char)pcp->readbuf[n+1]) &&
		    isdigit((int)(unsigned char)pcp->readbuf[n+2]) &&
		    isspace((int)(unsigned char)pcp->readbuf[n+3]))
		{
			pcp->readbuf[nn]=0;
			break;
		}
		n= ++nn;
	}

	return (0);
}

static int getonelinereply(struct PCPnet *pcp)
{
	size_t nn;
	int islast;

	nn=0;
	for (;;)
	{
		int ch;

		while ((ch=readch(pcp, nn)) != EOF)
		{
			if (ch == '\n')
				break;
			pcp->readbuf[nn++]=ch;
			if (nn >= 8192)
				nn=8192;
		}
		pcp->readbuf[nn]=0;
		if (ch == EOF)
			return (-1);

		if (!isdigit((int)(unsigned char)pcp->readbuf[0])
		    || !isdigit((int)(unsigned char)pcp->readbuf[1])
		    || !isdigit((int)(unsigned char)pcp->readbuf[2]))
		{
			nn=0;
			continue;
		}
		islast= pcp->readbuf[3] != '-';
		break;
	}
	return (islast);
}


static int docmd(struct PCPnet *pcp, const char *cmd, int cmdl)
{
	if (dowrite(pcp, cmd, cmdl) < 0)
		return (-1);
	return (getfullreply(pcp));
}

static int checkstatus(struct PCPnet *pcp, int *errcode)
{
	const char *p=strrchr(pcp->readbuf, '\n');
	int n;

	if (p)
		++p;
	else
		p=pcp->readbuf;
	n=atoi(p);
	if (errcode)
		switch (n) {
		case 504:
			*errcode=PCP_ERR_EVENTNOTFOUND;
			break;
		case 506:
			*errcode=PCP_ERR_EVENTLOCKED;
			break;
		}
	return (n);
}

static char *getword(struct PCPnet *pcp, char **p)
{
	char *q;

	while (**p && isspace((int)(unsigned char)**p))
		++*p;

	if (!**p)
		return (NULL);
	q= *p;

	while (**p && !isspace((int)(unsigned char)**p))
		++*p;

	if (**p)
	{
		**p=0;
		++*p;
	}
	return (q);
}

/* Parse 102 response */

static int parseauthtoken(struct PCPnet *pcp)
{
	char *p=pcp->readbuf;
	char *q;

	getword(pcp, &p);	/* skip 102 */

	q=getword(pcp, &p);

	if (!q)
	{
		errno=EIO;
		return (-1);
	}

	if (pcp->authtoken)
		free(pcp->authtoken);

	if ((p=strrchr(pcp->sockname, '/')) != 0)
		++p;
	else
		p=pcp->sockname;

	if ((pcp->authtoken=malloc(strlen(p)+strlen(q)+2)) == NULL)
		return (-1);
	strcat(strcat(strcpy(pcp->authtoken, p), "/"), q);
	return (0);
}

/* Parse 100 response */

static int has100(struct PCPnet *pcp, const char *kw)
{
	char *p=pcp->readbuf;
	int l = strlen(kw);

	while (*p)
	{
		while (isdigit((int)(unsigned char)*p))
			++p;
		if (*p != '\n')
			++p;

		if (strncasecmp(p, kw, l) == 0 &&
		    (p[l] == 0 || isspace((int)(unsigned char)p[l])))
			return (1);

		while (*p)
			if (*p++ == '\n')
				break;
	}
	return (0);
}

static int doconnect(struct PCPnet *pcp, const char *dir, const char *username,
		     const char *sockname,
		     const char *clustername,
		     char **errmsg)
{
	DIR *dirp=opendir(dir);
	struct sock_list *l=NULL;
	struct dirent *de;
	struct sock_list *nl;
	unsigned i,cnt=0;
	char **a;
	int fd;
	char *buf;
	int clustername_l=clustername ? strlen(clustername):0;

	if (errmsg)
		*errmsg=0;

	while (dirp && (de=readdir(dirp)) != NULL)
	{
		if (strchr(de->d_name, '.'))
			continue;

		if (sockname && strcmp(de->d_name, sockname))
			continue;

		/*
		** When the proxy connection comes in via the proxy cluster,
		** ignore the proxy cluster client's socket, so we don't end
		** up in an infinite loop!
		*/

		if (clustername)
		{
			const char *p=de->d_name;

			while (p && isdigit((int)(unsigned char)*p))
				++p;

			if (strncasecmp(p, clustername, clustername_l) == 0
			    && p[clustername_l] == '.')
				continue;
		}

		nl=malloc(sizeof(struct sock_list));

		if (!nl || (nl->filename=malloc(strlen(dir)+2+
						strlen(de->d_name))) == NULL)
		{
			if (nl) free(nl);

			while ((nl=l) != NULL)
			{
				l=nl->next;
				free(nl->filename);
				free(nl);
			}
			return (-1);
		}
		strcat(strcat(strcpy(nl->filename, dir), "/"), de->d_name);
		++cnt;
		nl->next=l;
		l=nl;
	}

	if (dirp)
		closedir(dirp);

	if (!l)
	{
		errno=ENOENT;
		return (-1);
	}

	if ((a=malloc(sizeof(char *)*cnt)) == NULL)
	{
		while ((nl=l) != NULL)
		{
			l=nl->next;
			free(nl->filename);
			free(nl);
		}
		return (-1);
	}

	cnt=0;
	for (nl=l; nl; nl=nl->next)
		a[cnt++]=nl->filename;

	qsort(a, cnt, sizeof(*a), cmp_str);

	fd= -1;

	for (i=0; i<cnt; i++)
	{
		struct  sockaddr_un skun;
		int rc;

		fd=socket(PF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			break;

		skun.sun_family=AF_UNIX;
		strcpy(skun.sun_path, a[i]);

		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
		{
			close(fd);
			fd= -1;
			break;
		}

		if ( pcp->sockname )
			free(pcp->sockname);
		if ( (pcp->sockname=strdup(a[i])) == NULL)
		{
			close(fd);
			fd= -1;
			break;
		}

		if ( connect(fd,
			     (const struct sockaddr *)&skun,
			     sizeof(skun)) == 0)
		{
			/* That was easy, we're done. */

			if (fcntl(fd, F_SETFL, 0) < 0)
			{
				close(fd);
				fd= -1;
				break;
			}
		}
		else if ( errno != EINPROGRESS && errno != EWOULDBLOCK)
		{
			close(fd);
			fd= -1;
			break;
		}
		else
		{
			struct timeval tv;
			fd_set fdr;
			int rc;

			FD_ZERO(&fdr);
			FD_SET(fd, &fdr);
			tv.tv_sec=30;
			tv.tv_usec=0;

			rc=select(fd+1, 0, &fdr, 0, &tv);
			if (rc <= 0 || !FD_ISSET(fd, &fdr))
			{
				close(fd);
				fd= -1;
				break;
			}

			if ( connect(fd, (const struct sockaddr *)&skun,
				     sizeof(skun)))
			{
				if (errno != EISCONN)
				{
					close(fd);
					break;
				}
			}

			if (fcntl(fd, F_SETFL, 0) < 0)
			{
				close(fd);
				break;
			}
		}

		pcp->fd=fd;
		if (docmd(pcp, "CAPABILITY\n", 0))
		{
			fd= -1;
			break;
		}

		if ((rc=checkstatus(pcp, NULL)) != 100)
		{
			close(fd);
			fd= -1;
			continue;
		}

		if (!has100(pcp, "PCP1"))
		{
			close(fd);
			fd= -1;
			continue;
		}

		buf=malloc(strlen(username)+sizeof("USERID \n"));
		if (buf == 0)
		{
			close(fd);
			fd= -1;
			break;

		}

		strcat(strcat(strcpy(buf, "USERID "), username), "\n");

		if (docmd(pcp, buf, 0))
		{
			fd= -1;
			free(buf);
			break;
		}
		pcp->fd= -1;
		free(buf);

		switch ((rc=checkstatus(pcp, NULL)) / 100) {
		case 1:
		case 2:
		case 3:
			break;
		default:
			errno=EIO;
			if (errmsg)
			{
				if (*errmsg)
					free(*errmsg);
				*errmsg=strdup(pcp->readbuf);
			}
			close(fd);
			fd= -1;
			break;
		case 5:
			errno=ENOENT;
			if (errmsg)
			{
				if (*errmsg)
					free(*errmsg);
				*errmsg=strdup("Calendar not found.");
			}
			close(fd);
			fd= -1;
			continue;
		}

		if (rc == 102)
		{
			if (parseauthtoken(pcp))
			{
				close(fd);
				fd= -1;
			}
			break;
		}
		break;
	}

	free(a);
	while ((nl=l) != NULL)
	{
		l=nl->next;
		free(nl->filename);
		free(nl);
	}
	return (fd);
}

static struct PCP *setcapabilities(struct PCPnet *, int);

struct PCP *pcp_open_server(const char *username, const char *password,
			    char **errmsg)
{
	struct PCPnet *pcp=mkpcp(username);

	if (errmsg)
		*errmsg=0;
	if (!pcp)
		return (NULL);

	if (strchr(username, '\r') || strchr(username, '\n'))
	{
		errno=EINVAL;
		pcp_close_net(pcp);
		return (NULL);
	}

	if (strchr(password, '\r') || strchr(password, '\n'))
	{
		errno=EINVAL;
		pcp_close_net(pcp);
		return (NULL);
	}


	if ((pcp->fd=doconnect(pcp, CALENDARDIR "/public", username, NULL,
			       NULL, errmsg)) < 0)
	{
		pcp_close_net(pcp);
		return (NULL);
	}

	if (pcp->authtoken == NULL)
	{
		char *buf;
		int rc;

		if (strchr(password, '\n') || strchr(password, '\r'))
		{
			errno=EINVAL;
			pcp_close_net(pcp);
			return (NULL);
		}

		buf=malloc(strlen(password)+sizeof("PASSWORD \n"));
		if (buf == 0)
		{
			pcp_close_net(pcp);
			return (NULL);
		}

		strcat(strcat(strcpy(buf, "PASSWORD "), password), "\n");

		if (docmd(pcp, buf, 0))
		{
			free(buf);
			pcp_close_net(pcp);
			return (NULL);
		}
		free(buf);

		switch ((rc=checkstatus(pcp, NULL)) / 100) {
		case 1:
		case 2:
		case 3:
			break;
		default:
			if (errmsg)
			{
				if (*errmsg)
					free(*errmsg);
				*errmsg=strdup(pcp->readbuf);
			}
			pcp_close_net(pcp);
			errno=EPERM;
			return (NULL);
		}

		if (rc == 102)
		{
			if (parseauthtoken(pcp))
			{
				pcp_close_net(pcp);
				return (NULL);
			}
		}
	}

	return (setcapabilities(pcp, 1));
}

static struct PCP *setcapabilities(struct PCPnet *pcp, int dofree)
{
	int rc;

	if (docmd(pcp, "CAPABILITY\n", 0))
	{
		if (dofree)
			pcp_close_net(pcp);
		return (NULL);
	}

	if ((rc=checkstatus(pcp, NULL)) != 100)
	{
		if (dofree)
			pcp_close_net(pcp);
		return (NULL);
	}

	if (!has100(pcp, "PCP1"))
	{
		if (dofree)
			pcp_close_net(pcp);
		return (NULL);
	}

	if (!has100(pcp, "ACL"))
	{
		pcp->pcp.acl_func=NULL;
		pcp->pcp.listacl_func=NULL;
	}
	return ((struct PCP *)pcp);
}

struct PCP *pcp_find_proxy(const char *username,
			   const char *clustername,
			   char **errmsg)
{
	struct PCPnet *pcp=mkpcp(username);

	if (errmsg)
		*errmsg=0;
	if (!pcp)
		return (NULL);

	if (strchr(username, '\r') || strchr(username, '\n'))
	{
		errno=EINVAL;
		pcp_close_net(pcp);
		return (NULL);
	}

	if ((pcp->fd=doconnect(pcp, CALENDARDIR "/private", username, NULL,
			       clustername,
			       errmsg)) < 0)
	{
		pcp_close_net(pcp);
		return (NULL);
	}
	return ((struct PCP *)pcp);
}

int pcp_set_proxy(struct PCP *pcp_ptr, const char *proxy)
{
	struct PCPnet *pcp=(struct PCPnet *)pcp_ptr;
	int rc;
	char *p;

	pcp->haserrmsg=0;

	if (strchr(proxy, '\r') || strchr(proxy, '\n'))
	{
		errno=EINVAL;
		return (-1);
	}

	p=malloc(strlen(proxy)+sizeof("PROXY \n"));

	if (!p)
		return (-1);

	strcat(strcat(strcpy(p, "PROXY "), proxy), "\n");

	if (docmd(pcp, p, 0))
	{
		free(p);
		return (-1);
	}

	free(p);
	switch ((rc=checkstatus(pcp, NULL)) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}

	if (setcapabilities(pcp, 0) == NULL)
		return (-1);
	return (0);
}

struct PCP *pcp_reopen_server(const char *username, const char *authtoken,
			      char **errmsg)
{
	struct PCPnet *pcp=mkpcp(username);
	char *authtoken_cpy;
	char *p, *q;

	if (!pcp)
		return (NULL);

	/* auth token is: sockname/token */

	if ((authtoken_cpy=strdup(authtoken)) == NULL)
	{
		pcp_close_net(pcp);
		return (NULL);
	}

	if ((p=strchr(authtoken_cpy, '/')) == NULL)
	{
		errno=EINVAL;
		free(authtoken_cpy);
		pcp_close_net(pcp);
		return (NULL);
	}

	*p++=0;

	for (q=p; *q; ++q)
		if (isspace((int)(unsigned char)*q))
		{
			errno=EINVAL;
			free(authtoken_cpy);
			pcp_close_net(pcp);
			return (NULL);
		}

	if ((pcp->fd=doconnect(pcp, CALENDARDIR "/public", username,
			       authtoken_cpy, NULL, errmsg)) < 0)
	{
		free(authtoken_cpy);
		pcp_close_net(pcp);
		return (NULL);
	}

	if (pcp->authtoken == NULL)
	{
		char *buf;
		int rc;

		buf=malloc(strlen(p)+sizeof("RELOGIN \n"));
		if (buf == 0)
		{
			free(authtoken_cpy);
			pcp_close_net(pcp);
			return (NULL);
		}

		strcat(strcat(strcpy(buf, "RELOGIN "), p), "\n");
		free(authtoken_cpy);

		if (docmd(pcp, buf, 0))
		{
			free(buf);
			pcp_close_net(pcp);
			return (NULL);
		}
		free(buf);

		switch ((rc=checkstatus(pcp, NULL)) / 100) {
		case 1:
		case 2:
		case 3:
			break;
		default:
			errno=EIO;
			if (errmsg)
			{
				if (*errmsg)
					free(*errmsg);
				*errmsg=strdup(pcp->readbuf);
			}
			pcp_close_net(pcp);
			return (NULL);
		}
		if (rc == 102)
		{
			if (parseauthtoken(pcp))
			{
				pcp_close_net(pcp);
				return (NULL);
			}
		}
		else	/* Keeping the same token */
			if ((pcp->authtoken=strdup(authtoken)) == NULL)
		{
			pcp_close_net(pcp);
			return (NULL);
		}
	}
	else
		free(authtoken_cpy);

	return (setcapabilities(pcp, 1));
}

static void pcp_close_quit_net(struct PCPnet *pcp)
{
	if (pcp->fd >= 0)
		docmd(pcp, "QUIT\n", 0);
	pcp_close_net(pcp);
}

static void pcp_close_net(struct PCPnet *pd)
{
	if (pd->fd >= 0)
		close(pd->fd);
	if (pd->sockname)
		free(pd->sockname);
	if (pd->authtoken)
		free(pd->authtoken);
	if (pd->readbuf)
		free(pd->readbuf);
	free(pd->username);
	free(pd);
}

static int cleanup(struct PCPnet *pn)
{
	return (0);
}

static struct PCP_new_eventid *neweventid(struct PCPnet *pn,
					  const char *ev,
					  struct PCP_save_event *se)
{
	struct PCPnet_new_eventid *p;
	char *q;
	char rbuf[BUFSIZ], wbuf[BUFSIZ];
	char *rbufptr;
	int bufl;
	char *wbufptr;
	int wbufleft;
	char *s;
	unsigned new_len, n;

	int seeneol;

	pn->haserrmsg=0;
	if (ev && (strchr(ev, '\n') || strchr(ev, '\r')))
	{
		errno=EINVAL;
		return (NULL);
	}

	p=malloc(sizeof(struct PCPnet_new_eventid));
	if (!p)
		return (NULL);
	memset(p, 0, sizeof(*p));

	pn->haserrmsg=1;

	if (docmd(pn, "RSET\n", 0))
	{
		free(p);
		return (NULL);
	}
	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		free(p);
		return (NULL);
	}

	if (docmd(pn, se->flags & PCP_OK_CONFLICT
		  ? "CONFLICT ON\n":"CONFLICT OFF\n", 0))
	{
		free(p);
		errno=EINVAL;
		return (NULL);
	}

	if (docmd(pn, se->flags & PCP_OK_PROXY_ERRORS
		  ? "FORCE ON\n":"FORCE OFF\n", 0))
	{
		free(p);
		errno=EINVAL;
		return (NULL);
	}

	if (ev)
	{
		pn->haserrmsg=0;
		q=malloc(sizeof("DELETE \n")+strlen(ev));
		if (!q)
		{
			free(p);
			return (NULL);
		}
		pn->haserrmsg=1;
		strcat(strcat(strcpy(q, "DELETE "), ev), "\n");
		if (docmd(pn, q, 0))
		{
			free(q);
			free(p);
			return (NULL);
		}
		free(q);

		switch (checkstatus(pn, NULL) / 100) {
		case 1:
		case 2:
		case 3:
			break;
		default:
			free(p);
			return (NULL);
		}
	}

	new_len=30;
	for (n=0; n<se->n_event_participants; n++)
	{
		const char *pp=se->event_participants[n].address;

		if (pp)
		{
			if (strchr(pp, '\n') || strchr(pp, '\r'))
			{
				errno=EINVAL;
				free(p);
				return (NULL);
			}

			new_len += strlen(pp)+1;
		}
	}

	if ((s=malloc(new_len)) == NULL)
	{
		free(p);
		return (NULL);
	}

	strcpy(s, "NEW");

	for (n=0; n<se->n_event_participants; n++)
	{
		const char *p=se->event_participants[n].address;

		if (p)
			strcat(strcat(s, " "), p);
	}

	strcat(s, "\n");

	if (docmd(pn, s, 0))
	{
		free(s);
		free(p);
		return (NULL);
	}
	free(s);

	if ((checkstatus(pn, NULL) / 100) != 3)
	{
		free(p);
		return (NULL);
	}

	wbufptr=wbuf;
	wbufleft=sizeof(wbuf);
	seeneol=1;

	while ((bufl=pcp_read_saveevent(se, rbuf, sizeof(rbuf))) > 0)
	{
		rbufptr=rbuf;

		while (bufl)
		{
			if (seeneol && *rbufptr == '.')
			{
				if (!wbufleft)
				{
					if (dowrite(pn, wbuf, sizeof(wbuf)))
						break;
					wbufptr=wbuf;
					wbufleft=sizeof(wbuf);
				}
				*wbufptr++='.';
				--wbufleft;
			}

			if (!wbufleft)
			{
				if (dowrite(pn, wbuf, sizeof(wbuf)))
					break;
				wbufptr=wbuf;
				wbufleft=sizeof(wbuf);
			}

			seeneol= *rbufptr == '\n';
			*wbufptr++ = *rbufptr++;
			--wbufleft;
			--bufl;
		}
	}

	if (bufl)	/* Write error, flush things through */
	{
		if (bufl > 0)
			while ((bufl=pcp_read_saveevent(se, rbuf,
							sizeof(rbuf))) > 0)
				;
		free(p);
		return (NULL);
	}

	s=seeneol ? ".\n":"\n.\n";

	while (*s)
	{
		if (!wbufleft)
		{
			if (dowrite(pn, wbuf, sizeof(wbuf)))
			{
				free(p);
				return (NULL);
			}
			wbufptr=wbuf;
			wbufleft=sizeof(wbuf);
		}
		*wbufptr++= *s++;
		--wbufleft;
	}

	if (wbufptr > wbuf && dowrite(pn, wbuf, wbufptr-wbuf))
	{
		free(p);
		return (NULL);
	}

	if (getfullreply(pn))
	{
		free(p);
		return (NULL);
	}

	if (checkstatus(pn, NULL) != 109)
	{
		errno=EIO;
		free(p);
		return (NULL);
	}

	s=pn->readbuf;

	getword(pn, &s);	/* Skip 109 */

	q=getword(pn, &s);
	if (!q)
	{
		errno=EIO;
		free(p);
		return (NULL);
	}
	if ((p->eventid.eventid=strdup(q)) == NULL)
	{
		free(p);
		return (NULL);
	}
	return (&p->eventid);
}

static void destroyeventid(struct PCPnet *pn, struct PCPnet_new_eventid *id)
{
	free(id->eventid.eventid);
	free(id);
}

static int docommitevent2(struct PCPnet *pn, int *,
			  void (*)(const char *, const char *, void *),
			  void *);

static int commitevent(struct PCPnet *pn, struct PCPnet_new_eventid *id,
		       struct PCP_commit *ci)
{
	if (!id->isbooked)
	{
		int rc=bookevent(pn, id, ci);

		if (rc)
			return (rc);
	}

	return (docommitevent2(pn, &ci->errcode, ci->proxy_callback,
			       ci->proxy_callback_ptr));
}

static int docommitevent2(struct PCPnet *pn, int *errcode,
			  void (*proxy_callback)(const char *,
						 const char *,
						 void *),
			  void *proxy_callback_ptr)
{
	int s;

	pn->haserrmsg=0;

	if (dowrite(pn, "COMMIT\n", 0))
		return (-1);

	pn->haserrmsg=1;

	while ((s=getonelinereply(pn)) >= 0)
	{
		int n=checkstatus(pn, errcode);

		if (n == 111)
		{
			char *p=pn->readbuf+3;
			char *action;

			if (*p)
				++p;


			action=getword(pn, &p);

			while (*p && isspace((int)(unsigned char)*p))
				++p;

			if (proxy_callback)
				(*proxy_callback)(action, p,
						  proxy_callback_ptr);
		}

		if (s > 0)
			return ( (n / 100) < 4 ? 0:-1);
	}
	pn->haserrmsg=0;
	return (-1);
}

static int docommitresponse(struct PCPnet *,
			    int (*)(const char *, time_t, time_t,
				    const char *, void *),
			    void *,
			    int *);

static int bookevent(struct PCPnet *pn, struct PCPnet_new_eventid *id,
		     struct PCP_commit *ci)
{
	char *q;
	unsigned i;
	int ss;

	ci->errcode=0;
	pn->haserrmsg=0;

	if (ci->n_event_times <= 0)
	{
		errno=EINVAL;
		return (-1);
	}

	pn->haserrmsg=1;

	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}

	/* yyyymmddhhmmss - 14 chars.  Each time is <space>start-end */

	pn->haserrmsg=0;

	q=malloc(ci->n_event_times * 32 + 20);	/* Eh, that's enough */

	if (!q)
		return (-1);

	strcpy(q, "BOOK");

	for (i=0; i<ci->n_event_times; i++)
	{
		char buf[15];

		pcp_gmtimestr(ci->event_times[i].start, buf);
		strcat(strcat(q, " "), buf);
		pcp_gmtimestr(ci->event_times[i].end, buf);
		strcat(strcat(q, "-"), buf);
	}
	strcat(q, "\n");

	if (dowrite(pn, q, 0))
	{
		free(q);
		return (-1);
	}
	free(q);

	ss=docommitresponse(pn, ci->add_conflict_callback,
			    ci->add_conflict_callback_ptr,
			    &ci->errcode);

	if (ss == 0)
		id->isbooked=1;
	return (ss);
}

static int docommitresponse(struct PCPnet *pn,
			    int (*conflict_func)(const char *, time_t, time_t,
						 const char *, void *),
			    void *callback_arg,
			    int *errcode)
{
	int s;
	int rc=0;

	pn->haserrmsg=0;

	while ((s=getonelinereply(pn)) >= 0)
	{
		int ss=checkstatus(pn, errcode);

		switch (ss / 100) {
		case 1:
		case 2:
		case 3:
			break;
		default:
			if (ss == 403)
			{
				char eventid[EVENTID_MAXLEN];
				char from[15];
				char to[15];
				char addr[ADDR_MAXLEN];
				char dummy;
				time_t from_t, to_t;

				if (sscanf(pn->readbuf,
					   "403%c" ADDR_SSCANF " %14s %14s "
					   EVENTID_SSCANF,
					   &dummy,
					   addr, from, to,
					   eventid)
				    != 5)
				{
					rc= -1;
					return (-1);
				}

				from_t=pcp_gmtime_s(from);
				to_t=pcp_gmtime_s(to);
				if (!from_t || !to_t)
				{
					errno=EIO;
					return (-1);
				}

				if (rc == 0 && conflict_func)
					rc= (*conflict_func)
						(eventid, from_t, to_t, addr,
						 callback_arg);
				if (errcode)
					*errcode=PCP_ERR_CONFLICT;
			}
			rc= -1;
		}
		pn->haserrmsg=1;
		if (s > 0)
			break;
		pn->haserrmsg=0;
	}

	return (rc);
}

static int parse105(struct PCPnet *pn, time_t *from_t, time_t *to_t,
		    char eventid[EVENTID_MAXLEN])
{
	char dummy;
	char from[15];
	char to[15];

	if (sscanf(pn->readbuf, "105%c" EVENTID_SSCANF " %14s %14s",
		   &dummy, eventid, from, to) == 4)
	{
		if ((*from_t=pcp_gmtime_s(from)) &&
		    (*to_t=pcp_gmtime_s(to)))
			return (0);
	}
	errno=EIO;
	return (-1);
}

static int listallevents(struct PCPnet *pn, struct PCP_list_all *la)
{
	char cmdbuf[100];
	int rc, s;

	strcpy(cmdbuf, "LIST");

	if (la->list_from || la->list_to)
	{
		char buf[15];

		strcat(cmdbuf, " FROM ");
		if (la->list_from)
		{
			pcp_gmtimestr(la->list_from, buf);
			strcat(cmdbuf, buf);
		}
		strcat(cmdbuf, "-");
		if (la->list_to)
		{
			pcp_gmtimestr(la->list_to, buf);
			strcat(cmdbuf, buf);
		}
	}

	strcat(cmdbuf, "\n");

	pn->haserrmsg=0;
	if (dowrite(pn, cmdbuf, 0) < 0)
		return (-1);

	rc=0;
	pn->haserrmsg=1;

	while ((s=getonelinereply(pn)) >= 0)
	{
		int n=checkstatus(pn, NULL);
		if (n >= 400)
			rc= -1;
		if (n == 105)
		{
			char eventid[EVENTID_MAXLEN];

			if (parse105(pn, &la->event_from, &la->event_to,
				     eventid) == 0)
			{
				la->event_id=eventid;
				if (rc == 0)
					rc= (*la->callback_func)
						(la, la->callback_arg);
			}
		}
		if (s > 0)
			break;
	}

	if (s < 0)
	{
		pn->haserrmsg=0;
		rc= -1;
	}
	return (rc);
}

static int cancelevent(struct PCPnet *pn, const char *id, int *errcode)
{
	char *buf;

	if (errcode)
		*errcode=0;

	pn->haserrmsg=0;

	if (strchr(id, '\r') || strchr(id, '\n'))
	{
		errno=EINVAL;
		return (-1);
	}

	buf=malloc(strlen(id)+20);
	if (!buf)
		return (-1);

	strcat(strcat(strcpy(buf, "CANCEL "), id), "\n");
	if (docmd(pn, buf, 0))
	{
		free(buf);
		return (-1);
	}
	pn->haserrmsg=1;

	switch (checkstatus(pn, errcode) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}
	return (0);
}

static int uncancelevent(struct PCPnet *pn, const char *id,
			 int flags, struct PCP_uncancel *ui)
{
	char *buf;

	pn->haserrmsg=0;
	if (ui)
		ui->errcode=0;

	if (strchr(id, '\r') || strchr(id, '\n'))
	{
		errno=EINVAL;
		return (-1);
	}
	if (docmd(pn, "RSET\n", 0))
		return (-1);
	pn->haserrmsg=1;

	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}

	pn->haserrmsg=0;
	if (docmd(pn, flags & PCP_OK_CONFLICT
		  ? "CONFLICT ON\n":"CONFLICT OFF\n", 0))
	{
		return (-1);
	}
	if (docmd(pn, flags & PCP_OK_PROXY_ERRORS
		  ? "FORCE ON\n":"FORCE OFF\n", 0))
	{
		return (-1);
	}

	pn->haserrmsg=1;

	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}

	buf=malloc(strlen(id)+20);
	if (!buf)
		return (-1);

	strcat(strcat(strcpy(buf, "UNCANCEL "), id), "\n");
	if (dowrite(pn, buf, 0))
	{
		free(buf);
		return (-1);
	}

	return (docommitresponse(pn, ui ? ui->uncancel_conflict_callback:NULL,
				 ui ? ui->uncancel_conflict_callback_ptr:NULL,
				 ui ? &ui->errcode:NULL));
}

static int deleteevent(struct PCPnet *pn,
		       struct PCP_delete *del)
{
	char *buf;

	pn->haserrmsg=0;
	del->errcode=0;

	if (strchr(del->id, '\r') || strchr(del->id, '\n'))
	{
		errno=EINVAL;
		return (-1);
	}
	if (docmd(pn, "RSET\n", 0))
		return (-1);
	pn->haserrmsg=1;

	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}
	pn->haserrmsg=0;

	buf=malloc(strlen(del->id)+20);
	if (!buf)
		return (-1);

	strcat(strcat(strcpy(buf, "DELETE "), del->id), "\n");
	if (docmd(pn, buf, 0))
	{
		free(buf);
		return (-1);
	}

	pn->haserrmsg=1;

	switch (checkstatus(pn, &del->errcode) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		return (-1);
	}

	return (docommitevent2(pn, &del->errcode,
			       del->proxy_callback,
			       del->proxy_callback_ptr));
}

static int retr_105(struct PCPnet *, struct PCP_retr *);
static int retr_106(struct PCPnet *, struct PCP_retr *);
static int retr_110(struct PCPnet *, struct PCP_retr *);
static int retr_107(struct PCPnet *, struct PCP_retr *, int);

static int retrevent(struct PCPnet *pn, struct PCP_retr *ri)
{
	char items_buf[256];
	unsigned i;
	size_t cnt;
	char *q;
	int errflag;

	items_buf[0]=0;
	pn->haserrmsg=0;

	if (ri->callback_retr_status)
		strcat(items_buf, " STATUS");
	if (ri->callback_retr_date)
		strcat(items_buf, " DATE");
	if (ri->callback_retr_participants)
		strcat(items_buf, " ADDR");
	if (ri->callback_rfc822_func)
		strcat(items_buf, " TEXT");
	else if (ri->callback_headers_func)
		strcat(items_buf, " HEADERS");

	if (items_buf[0] == 0)
	{
		errno=EIO;
		return (-1);
	}

	cnt=strlen(items_buf)+256;

	for (i=0; ri->event_id_list[i]; i++)
	{
		const char *p=ri->event_id_list[i];

		if (strchr(p, '\n'))
		{
			errno=EIO;
			return (-1);
		}
		cnt += 1 + strlen(p);
	}

	q=malloc(cnt);

	if (!q)
		return (-1);

	strcat(strcat(strcpy(q, "RETR"), items_buf), " EVENTS");

	for (i=0; ri->event_id_list[i]; i++)
	{
		strcat(strcat(q, " "), ri->event_id_list[i]);
	}
	strcat(q, "\n");

	if (dowrite(pn, q, 0) < 0)
	{
		free(q);
		return (-1);
	}
	free(q);

	errflag=0;

	for (;;)
	{
		int rc;

		if (!errflag)
			pn->haserrmsg=0;
		if (getfullreply(pn) < 0)
			return (-1);

		if (!errflag)
			pn->haserrmsg=1;
		rc=checkstatus(pn, NULL);

		if ( rc < 100 || rc >= 400)
			return (-1);
		if (rc == 108)
			break;
		pn->haserrmsg=0;

		switch (rc) {
		case 105:
			if (errflag)
				break;
			rc=retr_105(pn, ri);
			if (rc)
				errflag=rc;
			break;
		case 106:
			if (errflag)
				break;
			rc=retr_106(pn, ri);
			if (rc)
				errflag=rc;
			break;
		case 110:
			if (errflag)
				break;
			rc=retr_110(pn, ri);
			if (rc)
				errflag=rc;
			break;
		case 107:
			rc=retr_107(pn, ri, errflag);
			if (!errflag && rc)
				errflag=rc;
			break;
		default:
			close(pn->fd);
			pn->fd= -1;
			errno=EIO;
			return (-1);
		}
	}

	return (errflag);
}

static int retr_105(struct PCPnet *pn, struct PCP_retr *ri)
{
	char eventid[EVENTID_MAXLEN];
	time_t from_t, to_t;

	if (parse105(pn, &from_t, &to_t, eventid) == 0)
	{
		ri->event_id=eventid;

		if (ri->callback_retr_date)
			return ( (*ri->callback_retr_date)
				 (ri, from_t, to_t, ri->callback_arg));
	}

	return (0);
}

static int retr_106(struct PCPnet *pn, struct PCP_retr *ri)
{
	char dummy;
	char eventid[EVENTID_MAXLEN];
	char addr[ADDR_MAXLEN];

	if (sscanf(pn->readbuf, "106%c" EVENTID_SSCANF " " ADDR_SSCANF,
		   &dummy, eventid, addr) == 3)
	{
		ri->event_id=eventid;

		if (ri->callback_retr_participants)
			return ( (*ri->callback_retr_participants)
				 (ri, addr, NULL, ri->callback_arg));
	}

	return (0);
}

static int retr_110(struct PCPnet *pn, struct PCP_retr *ri)
{
	char dummy;
	char eventid[EVENTID_MAXLEN];

	if (sscanf(pn->readbuf, "110%c" EVENTID_SSCANF, 
		   &dummy, eventid) == 2)
	{
		const char *p, *q;
		char *r, *s;
		int flags=0;

		ri->event_id=eventid;

		p=pn->readbuf+4;
		while (p)
		{
			if (isspace((int)(unsigned char)*p))
				break;
			++p;
		}

		while (p)
		{
			if (!isspace((int)(unsigned char)*p))
				break;
			++p;
		}

		for (q=p; *q; q++)
		{
			if (isspace((int)(unsigned char)*q))
				break;
		}
		r=malloc(q-p+1);
		if (!r)
		{
			pn->haserrmsg=0;
			return (-1);
		}
		memcpy(r, p, q-p);
		r[q-p]=0;

		for (s=r; (s=strtok(s, ",")) != 0; s=0)
		{
			if (strcasecmp(s, "CANCELLED") == 0)
				flags |= LIST_CANCELLED;
			else if (strcasecmp(s, "BOOKED") == 0)
				flags |= LIST_BOOKED;
			else if (strcasecmp(s, "PROXY") == 0)
				flags |= LIST_PROXY;
		}


		if (ri->callback_retr_status)
			return ( (*ri->callback_retr_status)
				 (ri, flags, ri->callback_arg));
	}

	return (0);
}

static int retr_107(struct PCPnet *pn, struct PCP_retr *ri, int ignore)
{
	char dummy;
	char eventid[EVENTID_MAXLEN];
	int rc=0;
	int ch;
	int seeneol;
	int seendot;
	size_t nn;

	if (sscanf(pn->readbuf, "107%c" EVENTID_SSCANF,
		   &dummy, eventid) != 2)
	{
		errno=EIO;
		rc= -1;
	}

	ri->event_id=eventid;

	if (rc == 0 && ri->callback_begin_func)
		rc= (*ri->callback_begin_func)(ri, ri->callback_arg);

	seeneol=1;
	seendot=1;
	nn=0;

	ch=EOF;
	for (;;)
	{
		if (ch == EOF)
			ch=readch(pn, nn);
		if (ch == EOF)
		{
			rc= -1;
			break;
		}
		if (ch == '\r')
			continue;

		if (seeneol)
			seendot= ch == '.';
		else
		{
			if ( ch == '\n' && seendot)
				break;
			seendot=0;
		}
		seeneol= ch == '\n';

		if (!seendot)
			pn->readbuf[nn++]=ch;
		ch=EOF;

		if (ri->callback_rfc822_func)
		{
			if (nn >= 8192)
			{
				if (rc == 0)
					rc= (*ri->callback_rfc822_func)
						(ri, pn->readbuf, nn,
						 ri->callback_arg);
				nn=0;
			}
		}
		else if (ri->callback_headers_func)
		{
			if (nn > 8192)
				nn=8192;	/* Trim excessive hdrs */

			if (seeneol)
			{
				char *h;
				char *v;

				ch=readch(pn, nn);
				if (ch == EOF)
				{
					rc= -1;
					break;
				}

				if (ch != '\n' && isspace(ch))
				{
					/* Header wrapped */

					while (ch != EOF && ch != '\n'
					       && isspace(ch))
						ch=readch(pn, nn);
					pn->readbuf[nn-1]=' ';
					continue;
				}
				pn->readbuf[nn-1]=0;
				h=pn->readbuf;
				if ((v=strchr(h, ':')) == NULL)
					v="";
				else
				{
					*v++=0;
					while (*v &&
					       isspace((int)(unsigned char)*v))
						++v;
				}
				if (rc == 0)
					rc=(*ri->callback_headers_func)
						(ri, h, v, ri->callback_arg);
				nn=0;
			}
		}
		else nn=0;
	}

	if (ri->callback_rfc822_func)
	{
		if (rc == 0)
			rc= (*ri->callback_rfc822_func)
				(ri, pn->readbuf, nn, ri->callback_arg);
	}

	if (rc == 0 && ri->callback_end_func)
		rc= (*ri->callback_end_func)(ri, ri->callback_arg);
	return (rc);
}


static int setacl(struct PCPnet *pn, const char *who, int flags)
{
	char buf[1024];

	pn->haserrmsg=0;
	if (strchr(who, '\r') || strchr(who, '\n') || strlen(who) > 512)
	{
		errno=EINVAL;
		return (-1);
	}

	sprintf(buf, "ACL SET %s", who);
	pcp_acl_name(flags, buf);
	strcat(buf, "\n");

	if (docmd(pn, buf, 0))
		return (-1);
	pn->haserrmsg=1;
	switch (checkstatus(pn, NULL) / 100) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		errno=EIO;
		return (-1);
	}
	return (0);
}


static int listacl(struct PCPnet *pn, int (*func)(const char *, int, void *),
		   void *arg)
{
	int rc;
	int s;

	pn->haserrmsg=0;
	if (dowrite(pn, "ACL LIST\n", 0) < 0)
		return (-1);

	rc=0;
	pn->haserrmsg=1;

	while ((s=getonelinereply(pn)) >= 0)
	{
		int n=checkstatus(pn, NULL);
		if (n >= 400)
			rc= -1;
		if (n == 103)
		{
			char addr[ADDR_MAXLEN];
			char dummy;

			if (sscanf(pn->readbuf, "103%c" ADDR_SSCANF,
				   &dummy, addr) == 2)
			{
				const char *p=pn->readbuf+4;
				int flags=0;

				for ( ; *p; p++)
					if (isspace((int)(unsigned char)*p))
						break;

				while (*p)
				{
					const char *q;
					char buf[256];

					if (isspace((int)(unsigned char)*p))
					{
						++p;
						continue;
					}
					q=p;

					for ( ; *p; p++)
						if (isspace((int)
							    (unsigned char)*p))
							break;
					buf[0]=0;
					strncat(buf, q, p-q < 255 ? p-q:255);

					flags |= pcp_acl_num(buf);
				}

				if (rc == 0)
					rc= (*func)(addr, flags, arg);
			}
			else
			{
				if (rc == 0)
				{
					rc= -1;
					errno=EIO;
				}
			}
		}
		if (s > 0)
			break;
	}

	if (s < 0)
	{
		pn->haserrmsg=0;
		rc= -1;
	}
	return (rc);
}

static void noop(struct PCPnet *pd)
{
	docmd(pd, "NOOP\n", 0);
}
