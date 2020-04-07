/*
** Copyright 1998 - 2011 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"rfc1035.h"
#include	<stdio.h>
#include	<string.h>
#include	"soxwrap/soxwrap.h"
#include	<sys/types.h>
#include	<arpa/inet.h>
#include	<errno.h>
#include	<stdlib.h>
#if	HAVE_UNISTD_H
#include	<unistd.h>
#endif
#if TIME_WITH_SYS_TIME
#include	<sys/time.h>
#include	<time.h>
#else
#if HAVE_SYS_TIME_H
#include	<sys/time.h>
#else
#include	<time.h>
#endif
#endif

int rfc1035_open_udp(int *af)
{
	return (rfc1035_mksocket(SOCK_DGRAM, 0, af));
}

int rfc1035_send_udp(int fd, const struct sockaddr *sin, int sin_len,
	const char *query, unsigned query_len)
{
	if (sox_sendto(fd, (const char *)query, query_len, 0,
		sin, sin_len) == query_len)
		return (0);
	return (-1);
}


struct rfc1035_udp_query_responses *
rfc1035_udp_query_response_alloc(const char **queries,
				 const unsigned *querylens,
				 int n_queries)
{
	struct rfc1035_udp_query_response *buf=
		calloc(n_queries, sizeof(struct rfc1035_udp_query_response));
	int n;

	struct rfc1035_udp_query_responses *resps;

	if (!buf)
		return 0;

	resps=malloc(sizeof(struct rfc1035_udp_query_responses));

	if (!resps)
	{
		free(buf);
		return 0;
	}
	resps->n_queries=n_queries;
	resps->queries=buf;

	for (n=0; n<n_queries; ++n)
	{
		buf[n].query=queries[n];
		buf[n].querylen=querylens[n];
	}

	return resps;
}

void rfc1035_udp_query_response_free(struct rfc1035_udp_query_responses *resps)
{
	int n;

	for (n=0; n<resps->n_queries; ++n)
	{
		if (resps->queries[n].response)
			free(resps->queries[n].response);
	}

	free(resps->queries);
	free(resps);
}

static int dorecv(int fd, char *bufptr, unsigned buflen, int flags,
		struct sockaddr *addr, socklen_t *addrlen)
{
socklen_t len;

	do
	{
		len=sox_recvfrom(fd, bufptr, buflen, flags, addr, addrlen);
	} while (len < 0 && errno == EINTR);
	return (len);
}

static int rfc1035_recv_one_udp_response(int fd,
	const struct sockaddr *addrshouldfrom, int addrshouldfrom_len,
        struct rfc1035_udp_query_responses *queries)
{
int	len;

#if	RFC1035_IPV6

struct sockaddr_storage addrfrom;

#else

struct sockaddr_in addrfrom;

#endif

socklen_t addrfromlen;
char	rfc1035_buf[512];
char	*bufptr=rfc1035_buf;
char	*mallocedbuf=0;
int     i;
unsigned     buflen=sizeof(rfc1035_buf);

	while ((len=dorecv(fd, bufptr, buflen, MSG_PEEK, 0, 0)) >= buflen )
	{
		if (len == buflen)	len += 511;
		++len;

		if (mallocedbuf)	free(mallocedbuf);
		mallocedbuf=(char *)malloc(len);
		if (!mallocedbuf)	return (0);
		bufptr= mallocedbuf;
		buflen=len;
	}

	addrfromlen=sizeof(addrfrom);
	if (len < 0 || (len=dorecv(fd, bufptr, buflen, 0,
		(struct sockaddr *)&addrfrom, &addrfromlen)) < 0)
	{
		if (mallocedbuf)
			free(mallocedbuf);
		errno=EIO;
		return (0);
	}

	buflen=len;

	if ( !rfc1035_same_ip( &addrfrom, addrfromlen,
				addrshouldfrom, addrshouldfrom_len))
	{
		if (mallocedbuf)
			free(mallocedbuf);

		errno=EAGAIN;
		return (0);
	}

	if ( buflen < 2)
	{
		if (mallocedbuf)
			free(mallocedbuf);
		errno=EAGAIN;
		return (0);
	}

	for (i=0; i<queries->n_queries; ++i)
	{
		if (queries->queries[i].response)
			continue; /* Already received this one */

		if ( bufptr[0] != queries->queries[i].query[0] ||
		     bufptr[1] != queries->queries[i].query[1]
		     || (unsigned char)(bufptr[2] & 0x80) == 0 )
			continue;

		if (!mallocedbuf)
		{
			if ((mallocedbuf=malloc( buflen )) == 0)
				return (0);

			memcpy(mallocedbuf, bufptr, buflen);
			bufptr=mallocedbuf;
		}

		queries->queries[i].response=mallocedbuf;
		queries->queries[i].resplen=buflen;
		return 1;
	}

	if (mallocedbuf)
		free(mallocedbuf);
	errno=EAGAIN;
	return (0);
}

char *rfc1035_query_udp(struct rfc1035_res *res,
	int fd, const struct sockaddr *sin, int sin_len,
	const char *query, unsigned query_len, int *buflen, unsigned w)
{
	struct rfc1035_udp_query_responses *resps=
		rfc1035_udp_query_response_alloc(&query, &query_len, 1);
	char *bufptr=0;

	if (!resps)
		return 0;

	if (rfc1035_udp_query_multi(res, fd, sin, sin_len, resps, w))
	{
		bufptr=resps->queries[0].response;
		*buflen=resps->queries[0].resplen;
		resps->queries[0].response=0;
	}

	rfc1035_udp_query_response_free(resps);
	return bufptr;
}

int rfc1035_udp_query_multi(struct rfc1035_res *res,
			    int fd, const struct sockaddr *sin, int sin_len,
			    struct rfc1035_udp_query_responses *qr,
			    unsigned w)
{
time_t current_time, final_time;
int     i;

	time(&current_time);

	for (i=0; i<qr->n_queries; ++i)
	{
		if (qr->queries[i].response)
			continue; /* Already sent it */

		if (rfc1035_send_udp(fd, sin, sin_len,
				     qr->queries[i].query,
				     qr->queries[i].querylen))
			return (0);
	}
	final_time=current_time+w;

	while (1)
	{
		for (i=0; i<qr->n_queries; ++i)
			if (!qr->queries[i].response)
				break;

		if (i == qr->n_queries)
			return 1; /* Everything received */

		if (current_time >= final_time)
			break;

		if (rfc1035_wait_reply(fd, final_time-current_time))
			break;

		if (!rfc1035_recv_one_udp_response(fd,
						   sin,
						   sin_len,
						   qr))
		{
			if (errno != EAGAIN)
				return 0;
		}

		time(&current_time);
	}
	errno=EAGAIN;
	return (0);
}
