/*
** Copyright 1998 - 2011 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	"rfc1035.h"
#include	"spf.h"
#include	<sys/types.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<arpa/inet.h>

#include	"soxwrap/soxwrap.h"


static void setns(const char *p, struct rfc1035_res *res)
{
RFC1035_ADDR ia[4];
int	i=0;
char	*q=malloc(strlen(p)+1), *r;

	strcpy(q, p);
	for (r=q; (r=strtok(r, ", ")) != 0; r=0)
		if (i < 4)
		{
			if (rfc1035_aton(r, &ia[i]) == 0)
			{
				++i;
			}
			else
			{
				fprintf(stderr, "%s: invalid IP address\n",
					r);
			}
		}
	free(q);
	rfc1035_init_ns(res, ia, i);
}

extern char rfc1035_spf_gettxt(const char *current_domain,
			       char *buf);
extern char rfc1035_spf_gettxt_n(const char *current_domain,
			  char **buf);


static void spflookup(const char *current_domain)
{
	char *buf;

	switch (rfc1035_spf_gettxt_n(current_domain, &buf)) {
	case SPF_NONE:
		printf("none\n");
		return;
	case SPF_NEUTRAL:
		printf("neutral\n");
		return;
	case SPF_PASS:
		printf("pass: %s\n", buf);
		return;
	case SPF_FAIL:
		printf("fail\n");
		return;
	case SPF_SOFTFAIL:
		printf("softfail\n");
		return;
	case SPF_ERROR:
		printf("error\n");
		return;
	default:
		printf("unknown\n");
	}
}

static int get_q_type_pass(const char *p,
			    void (*cb)(unsigned char, void *),
			    void *ptr)
{
	char qbuf[strlen(p)+1];
	char *q;

	strcpy(qbuf, p);

	for (q=qbuf; (q=strtok(q, ", \t\r\n")); q=0)
	{
		int n=rfc1035_type_strtoi(q);

		if (n < 0)
			return -1;

		(*cb)(n, ptr);
	}

	return 0;
}

static void get_q_type_count(unsigned char c, void *ptr)
{
	++*(size_t *)ptr;
}

static void get_q_type_save(unsigned char c, void *ptr)
{
	*(*(unsigned char **)ptr)++=c;
}

static unsigned char *get_q_type(const char *p)
{
	size_t n=0;
	unsigned char *buf, *q;

	errno=EINVAL;
	if (get_q_type_pass(p, &get_q_type_count, &n) < 0)
		return 0;

	if ((buf=(unsigned char *)malloc(n+1)) == 0)
		return 0;

	q=buf;
	get_q_type_pass(p, &get_q_type_save, &q);

	*q=0;

	return buf;
}

int main(int argc, char **argv)
{
struct  rfc1035_res res;
struct	rfc1035_reply *replyp;
int	argn;
const char *q_name;
unsigned char *q_type;
int	q_class;
int	q_xflag=0;
int	q_rflag=0;
char	ptrbuf[RFC1035_MAXNAMESIZE+1];

	rfc1035_init_resolv(&res);

	argn=1;
	while (argn < argc)
	{
		if (argv[argn][0] == '@')
		{
			setns(argv[argn]+1, &res);
			++argn;
			continue;
		}

		if (strcmp(argv[argn], "-x") == 0)
		{
			q_xflag=1;
			++argn;
			continue;
		}
		if (strcmp(argv[argn], "-r") == 0)
		{
			q_rflag=1;
			++argn;
			continue;
		}

		if (strcmp(argv[argn], "-dnssec") == 0)
		{
			rfc1035_init_dnssec_enable(&res, 1);
			++argn;
			continue;
		}

		if (strcmp(argv[argn], "-udpsize") == 0)
		{
			++argn;

			if (argn < argc)
			{
				rfc1035_init_edns_payload(&res,
							  atoi(argv[argn]));
				++argn;
			}
			continue;
		}

		break;
	}

	if (argn >= argc)	exit(0);

	q_name=argv[argn++];

	if (q_xflag)
	{
	struct in_addr ia;
#if	RFC1035_IPV6
	struct in6_addr ia6;

		if (inet_pton(AF_INET6, q_name, &ia6) > 0)
		{
		const char *sin6=(const char *)&ia6;
		unsigned i;

			ptrbuf[0]=0;

			for (i=sizeof(struct in6_addr); i; )
			{
			char    buf[10];

				--i;
				sprintf(buf, "%x.%x.",
					(int)(unsigned char)(sin6[i] & 0x0F),
					(int)(unsigned char)((sin6[i] >> 4)
							& 0x0F));
				strcat(ptrbuf, buf);
			}
			strcat(ptrbuf, "ip6.arpa");
			q_name=ptrbuf;
		}
		else
#endif
		if ( rfc1035_aton_ipv4(q_name, &ia) == 0)
		{
		char buf[RFC1035_MAXNAMESIZE];
		unsigned char a=0,b=0,c=0,d=0;
		const char *p=buf;

			rfc1035_ntoa_ipv4(&ia, buf);

			while (*p >= '0' && *p <= '9')
				a= (int)a * 10 + (*p++ - '0');
			if (*p)	p++;
			while (*p >= '0' && *p <= '9')
				b= (int)b * 10 + (*p++ - '0');
			if (*p)	p++;
			while (*p >= '0' && *p <= '9')
				c= (int)c * 10 + (*p++ - '0');
			if (*p)	p++;
			while (*p >= '0' && *p <= '9')
				d= (int)d * 10 + (*p++ - '0');

			sprintf(ptrbuf, "%d.%d.%d.%d.in-addr.arpa",
				(int)d, (int)c, (int)b, (int)a);
			q_name=ptrbuf;
		}
	}

	if (q_rflag)
	{
	RFC1035_ADDR a;
	int	rc;

		if (rfc1035_aton(q_name, &a) == 0)
		{
			rc=rfc1035_ptr(&res, &a,ptrbuf);
			if (rc == 0)
			{
				printf("%s\n", ptrbuf);
				exit(0);
			}
		}
		else
		{
		RFC1035_ADDR	*aptr;
		unsigned alen;

			rc=rfc1035_a(&res, q_name, &aptr, &alen);
			if (rc == 0)
			{
			unsigned i;

				for (i=0; i<alen; i++)
				{
					rfc1035_ntoa(&aptr[i], ptrbuf);
					printf("%s\n", ptrbuf);
				}
				exit(0);
			}
		}
		fprintf(stderr, "%s error.\n", errno == ENOENT ? "Hard":"Soft");
		exit(1);
	}

	q_type=0;

	if (argn < argc)
	{
		if (strcmp(argv[argn], "spf") == 0)
		{
			spflookup(q_name);
			exit(0);
		}
		q_type=get_q_type(argv[argn]);
		if (!q_type)
		{
			perror(argv[argn]);
			exit(1);
		}
		argn++;
	}

	if (q_type == 0)
		q_type=get_q_type(q_xflag ? "PTR":"ANY");

	q_class= -1;
	if (argn < argc)
		q_class=rfc1035_class_strtoi(argv[argn]);
	if (q_class < 0)
		q_class=RFC1035_CLASS_IN;

	if (q_type[0] && q_type[1])
	{
		char namebuf[RFC1035_MAXNAMESIZE+1];

		size_t l=strlen(q_name);

		if (l > RFC1035_MAXNAMESIZE)
			l=RFC1035_MAXNAMESIZE;

		memcpy(namebuf, q_name, l);
		namebuf[l]=0;

		if (rfc1035_resolve_cname_multiple(&res, namebuf,
						   q_type, q_class,
						   &replyp,
						   RFC1035_X_RANDOMIZE)
		    < 0)
			replyp=0;
	}
	else
	{
		replyp=rfc1035_resolve(&res, RFC1035_OPCODE_QUERY,
				       q_name, q_type[0], q_class);
	}
	free(q_type);
	if (!replyp)
	{
		perror(argv[0]);
		exit(1);
	}

	if (q_type[0] && q_type[1])
	{
		struct rfc1035_reply *q;

		for (q=replyp; q; q=q->next)
		{
			struct rfc1035_reply *s=q->next;

			q->next=0;
			rfc1035_dump(q, stdout);
			q->next=s;
		}
	}
	else
	{
		rfc1035_dump(replyp, stdout);
	}
	rfc1035_replyfree(replyp);
	rfc1035_destroy_resolv(&res);
	return (0);
}
