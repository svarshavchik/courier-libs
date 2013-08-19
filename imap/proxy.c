/*
** Copyright 2004-2005 Double Precision, Inc.
** See COPYING for distribution information.
*/

#if	HAVE_CONFIG_H
#include	"config.h"
#endif
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<time.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if	HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<netdb.h>
#if HAVE_SYS_SELECT_H
#include	<sys/select.h>
#endif
#include	<sys/time.h>
#if HAVE_POLL
#include	<sys/poll.h>
#endif
#include	<courierauthdebug.h>
#include	"proxy.h"


static int checkhostname(const char *host)
{
	char hostbuf[256];
	const char *proxyhostname;

	if ((proxyhostname=getenv("PROXY_HOSTNAME")) != NULL)
	{
		if (strcmp(proxyhostname, host) == 0)
		{
			courier_authdebug_printf("Proxy host %s is me, normal login.",
						 host);
			return -1;
		}
	}

	if (gethostname(hostbuf, sizeof(hostbuf)))
	{
		courier_authdebug_printf
			("gethostname failed: %s",
			 strerror(errno));
		return -1;
	}

	if (strcmp(hostbuf, host) == 0)
	{
		courier_authdebug_printf("Proxy host %s is me, normal login.",
					 host);
		return -1;
	}
	return 0;
}

static int connect_host(struct proxyinfo *pi, const char *host);

int connect_proxy(struct proxyinfo *pi)
{
	char *h=strdup(pi->host);
	char *p, *q;
	int fd;

	if (!h)
	{
		courier_authdebug_printf("%s", strerror(errno));
		return -1;
	}

	for (p=h; *p;)
	{
		if (*p == ',')
		{
			++p;
			continue;
		}

		for (q=p; *q; q++)
			if (*q == ',')
				break;
		if (*q)
			*q++=0;

		fd=connect_host(pi, p);
		if (fd >= 0)
		{
			free(h);
			return fd;
		}
		p=q;
	}
	free(h);
	return -1;
}


static int proxyconnect(struct proxyinfo *pi,
			int aftype,
			void *addr,
			size_t);

#if HAVE_GETADDRINFO

static int connect_host(struct proxyinfo *pi, const char *host)
{
	int fd;
	char portbuf[40];
	int errcode;
	struct addrinfo hints, *res, *p;

	if (checkhostname(host))
		return (0);

	sprintf(portbuf, "%d", pi->port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family=PF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;

	errcode=getaddrinfo(host, portbuf, &hints, &res);

	if (errcode)
	{
		courier_authdebug_printf
			("getaddrinfo on proxyhost %s failed: %s",
			 pi->host, gai_strerror(errcode));
		return (-1);
	}

	for (p=res; p; p=p->ai_next)
	{
		if ((fd=proxyconnect(pi, p->ai_family,
				     p->ai_addr,
				     p->ai_addrlen))
		    >= 0)
		{
			if ((*pi->connected_func)(fd, host,
						  pi->void_arg))
			{
				close(fd);
				courier_authdebug_printf
					("Failed: %s.", strerror(errno));
				continue;
			}
			freeaddrinfo(res);
			return fd;
		}
	}
	freeaddrinfo(res);
	courier_authdebug_printf
		("Connection to proxyhost %s failed.", host);
	return (-1);
}

#else
static int connect_host(struct proxyinfo *pi, const char *host)
{
	struct hostent *he;
	int i;
	int fd;

	if (checkhostname(host))
		return (0);

	he=gethostbyname(host);

	if (he == NULL)
	{
		courier_authdebug_printf
			("gethostbyname on proxyhost %s failed.",
			 host);
		return (-1);
	}

	for (i=0; he->h_addr_list[i]; i++)
	{
		switch (he->h_addrtype) {
		case AF_INET:
			{
				struct sockaddr_in sin;

				memset(&sin, 0, sizeof(sin));

				sin.sin_family=AF_INET;

				memcpy(&sin.sin_addr, he->h_addr_list[i],
				       sizeof(sin.sin_addr));
				sin.sin_port=htons(pi->port);

				fd=proxyconnect(pi, PF_INET, &sin,
						sizeof(sin));
			}
			break;
#ifdef AF_INET6
		case AF_INET6:
			{
				struct sockaddr_in6 sin6;

				memset(&sin6, 0, sizeof(sin6));

				sin6.sin6_family=AF_INET6;

				memcpy(&sin6.sin6_addr, he->h_addr_list[i],
				       sizeof(sin6.sin6_addr));

				sin6.sin6_port=htons(pi->port);
				fd=proxyconnect(pi, PF_INET6, &sin6,
						sizeof(sin6));
			}
			break;
#endif
		default:
			courier_authdebug_printf
				("Unknown address family type %d",
				 he->h_addrtype);
			continue;
		}

		if (fd >= 0)
		{
			if ((*pi->connected_func)(fd, host,
						  pi->void_arg))
			{
				close(fd);
				courier_authdebug_printf
					("Failed: %s.", strerror(errno));
				continue;
			}
			return fd;
		}
	}
	courier_authdebug_printf
		("Connection to proxyhost %s failed.", host);
	return (-1);
}
#endif

#if HAVE_POLL

static int proxy_waitfd(int fd, int waitWrite, const char *hostnamebuf)
{
	struct pollfd pfd;

	memset(&pfd, 0, sizeof(pfd));

	pfd.fd=fd;
	pfd.events= waitWrite ? POLLOUT:POLLIN;

	if (poll(&pfd, 1, 30 * 1000) < 0)
	{
		courier_authdebug_printf
			("Poll failed while waiting to connect to %s: %s",
			 hostnamebuf, strerror(errno));
		return -1;
	}

	if (pfd.revents & (POLLOUT|POLLIN))
		return 0;

	courier_authdebug_printf
		("Timeout/error connecting to %s", hostnamebuf);
	return -1;
}

#else
static int proxy_waitfd(int fd, int waitWrite, const char *hostnamebuf)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec=30;
	tv.tv_usec=0;

	if (select(fd+1, waitWrite ? NULL:&fds,
		   waitWrite ? &fds:NULL, NULL, &tv) < 0)
	{
		courier_authdebug_printf
			("Select failed while waiting to connect to %s: %s",
			 hostnamebuf, strerror(errno));

		return -1;
	}

	if (!FD_ISSET(fd, &fds))
	{
		courier_authdebug_printf
			("Timeout connecting to %s", hostnamebuf);
		return -1;
	}

	return 0;
}
#endif

static int proxyconnect(struct proxyinfo *pi,
			int pf,
			void *addr,
			size_t addrLen)
{
	int fd;
	char hostnamebuf[256];
	int errcode;
	socklen_t errcode_l;

	struct sockaddr *sa=(struct sockaddr *)addr;

	switch (sa->sa_family) {
	case AF_INET:
		{
			struct sockaddr_in *sin=(struct sockaddr_in *)sa;

			strcpy(hostnamebuf, inet_ntoa(sin->sin_addr));
		}
		break;

#ifdef AF_INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6=
				(struct sockaddr_in6 *)sa;

			if (inet_ntop(AF_INET6, &sin6->sin6_addr,
				      hostnamebuf,
				      sizeof(hostnamebuf)) == NULL)
				strcpy(hostnamebuf, "inet_ntop() failed");
		}
		break;
#endif
	}


	fd=socket(pf, SOCK_STREAM, 0);

	if (fd < 0)
	{
		courier_authdebug_printf("socket: %s", strerror(errno));
		return (-1);
	}

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		close(fd);
		courier_authdebug_printf("fcntl(socket): %s", strerror(errno));
	}

	if (connect(fd, addr, addrLen) == 0)
		return fd;

	if (errno != EINPROGRESS)
	{
		courier_authdebug_printf
			("Proxy connection to %s failed: %s",
			 hostnamebuf, strerror(errno));
		close(fd);
		return -1;
	}

	if (proxy_waitfd(fd, 1, hostnamebuf))
	{
		close(fd);
		return -1;
	}

	errcode_l=sizeof(errcode);

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &errcode, &errcode_l) < 0)
	{
		courier_authdebug_printf
			("getsockopt failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (errcode)
	{
		courier_authdebug_printf
			("Proxy connection to %s failed: %s", hostnamebuf,
			 strerror(errcode));
		close(fd);
		return -1;
	}

	return fd;
}

static int proxy_getch(int fd, struct proxybuf *pb)
{
	if (pb->bufleft == 0)
	{
		int n;

		if (proxy_waitfd(fd, 0, "server"))
			return -1;

		pb->bufptr=pb->buffer;

		n=read(fd, pb->buffer, sizeof(pb->buffer));

		if (n < 0)
		{
			courier_authdebug_printf
				("Connection error: %s",
				 strerror(errno));
			return -1;
		}

		if (n == 0)
		{
			courier_authdebug_printf
				("Connection closed by remote host");
			return -1;
		}

		pb->bufleft=(size_t)n;
	}
	--pb->bufleft;
	return ((int)(unsigned char)*pb->bufptr++);
}

int proxy_readline(int fd, struct proxybuf *pb,
		   char *linebuf,
		   size_t linebuflen,
		   int imapmode)
{
	size_t i;
	int ch;
	int prevch;

	i=0;

	ch=0;

	do
	{
		prevch=ch;

		ch=proxy_getch(fd, pb);

		if (ch < 0)
			return -1;

		if (i < linebuflen)
			linebuf[i++]=(char)ch;
	} while (ch != '\n' || (imapmode && prevch != '\r'));

	if (i)
		linebuf[--i]=0;

	if (i && linebuf[i-1] == '\r')
		linebuf[--i]=0;

	DPRINTF("Received: %s", linebuf);

	return 0;
}

int proxy_write(int fd, const char *hostname,
		const char *buf, size_t buf_len)
{
	DPRINTF("Sending: %s", buf);

	while (buf_len)
	{
		int n;

		if (proxy_waitfd(fd, 1, hostname))
			return -1;

		n=write(fd, buf, buf_len);

		if (n < 0)
		{
			courier_authdebug_printf
				("Error sending to %s: %s",
				 hostname, strerror(errno));
			return -1;
		}

		if (n == 0)
		{
			courier_authdebug_printf
				("Connection close by %s", hostname);
			return -1;
		}

		buf_len -= n;
		buf += n;
	}
	return 0;
}

#if HAVE_POLL
void proxyloop(int fd)
{
	char stdin_buf[BUFSIZ];
	char stdout_buf[BUFSIZ];

	char *stdin_ptr=NULL;
	char *stdout_ptr=NULL;
	size_t stdin_left=0;
	size_t stdout_left=0;
	int n;

	struct pollfd pfd[2];

	if (fcntl(0, F_SETFL, O_NONBLOCK) ||
	    fcntl(1, F_SETFL, O_NONBLOCK))
	{
		courier_authdebug_printf("fcntl: %s",
					 strerror(errno));
		return;
	}

	do
	{
		memset(&pfd, 0, sizeof(pfd));

		if (stdin_left == 0)
		{
			pfd[0].fd=0;
			pfd[0].events=POLLIN;
		}
		else
		{
			pfd[0].fd=fd;
			pfd[0].events=POLLOUT;
		}

		if (stdout_left == 0)
		{
			pfd[1].fd=fd;
			pfd[1].events=POLLIN;
		}
		else
		{
			pfd[1].fd=1;
			pfd[1].events=POLLOUT;
		}

		n=1;

		if (poll(pfd, 2, -1) < 0)
		{
			courier_authdebug_printf("poll: %s",
						 strerror(errno));
			continue;
		}

		if (stdin_left == 0)
		{
			if (pfd[0].revents)
			{
				n=read(0, stdin_buf, sizeof(stdin_buf));

				if (n > 0)
				{
					stdin_ptr=stdin_buf;
					stdin_left=(size_t)n;
				}
			}
		}
		else if (pfd[0].revents)
		{
			n=write(fd, stdin_ptr, stdin_left);

			if (n > 0)
			{
				stdin_ptr += n;
				stdin_left -= n;
			}
		}

		if (n > 0)
		{
			if (stdout_left == 0)
			{
				if (pfd[1].revents)
				{
					n=read(fd, stdout_buf,
					       sizeof(stdout_buf));
				
					if (n > 0)
					{
						stdout_ptr=stdout_buf;
						stdout_left=(size_t)n;
					}
				}
			} else if (pfd[1].revents)
			{
				n=write(1, stdout_ptr, stdout_left);

				if (n > 0)
				{
					stdout_ptr += n;
					stdout_left -= n;
				}
			}
		}
	} while (n > 0);

	if (n < 0)
		courier_authdebug_printf("%s", strerror(errno));
}

#else
void proxyloop(int fd)
{
	char stdin_buf[BUFSIZ];
	char stdout_buf[BUFSIZ];

	char *stdin_ptr=NULL;
	char *stdout_ptr=NULL;
	size_t stdin_left=0;
	size_t stdout_left=0;
	int n;

	fd_set fdr, fdw;

	if (fcntl(0, F_SETFL, O_NONBLOCK) ||
	    fcntl(1, F_SETFL, O_NONBLOCK))
	{
		courier_authdebug_printf("fcntl: %s",
					 strerror(errno));
		return;
	}

	do
	{
		FD_ZERO(&fdr);
		FD_ZERO(&fdw);

		if (stdin_left == 0)
			FD_SET(0, &fdr);
		else
			FD_SET(fd, &fdw);

		if (stdout_left == 0)
			FD_SET(fd, &fdr);
		else
			FD_SET(1, &fdw);

		n=1;

		if (select(fd+1, &fdr, &fdw, NULL, NULL) < 0)
		{
			courier_authdebug_printf("select: %s",
						 strerror(errno));
			continue;
		}

		if (stdin_left == 0)
		{
			if (FD_ISSET(0, &fdr))
			{
				n=read(0, stdin_buf, sizeof(stdin_buf));

				if (n > 0)
				{
					stdin_ptr=stdin_buf;
					stdin_left=(size_t)n;
				}
			}
		}
		else if (FD_ISSET(fd, &fdw))
		{
			n=write(fd, stdin_ptr, stdin_left);

			if (n > 0)
			{
				stdin_ptr += n;
				stdin_left -= n;
			}
		}

		if (n > 0)
		{
			if (stdout_left == 0)
			{
				if (FD_ISSET(fd, &fdr))
				{
					n=read(fd, stdout_buf,
					       sizeof(stdout_buf));
				
					if (n > 0)
					{
						stdout_ptr=stdout_buf;
						stdout_left=(size_t)n;
					}
				}
			} else if (FD_ISSET(1, &fdw))
			{
				n=write(1, stdout_ptr, stdout_left);

				if (n > 0)
				{
					stdout_ptr += n;
					stdout_left -= n;
				}
			}
		}
	} while (n > 0);

	if (n < 0)
		courier_authdebug_printf("%s", strerror(errno));
}
#endif
