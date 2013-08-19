/*
** Copyright 2006, Double Precision Inc.
**
** See COPYING for distribution information.
*/

#include "libldapsearch.h"
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/*
** Allocate and deallocate the ldapsearch struct.
*/

struct ldapsearch *l_search_alloc(const char *host,
				  int port,
				  const char *userid,
				  const char *password,
				  const char *base)
{
	char *buf;

	struct ldapsearch *p=(struct ldapsearch *)
		malloc(sizeof(struct ldapsearch));

	if (!p)
		return NULL;

	if ((p->base=strdup(base)) == NULL)
	{
		free(p);
		return NULL;
	}

	if ((buf=malloc(strlen(host)+100)) == NULL)
	{
		free(p->base);
		free(p);
		return NULL;
	}

	sprintf(buf, "ldap://%s:%d", host, port);

	if (ldap_initialize(&p->handle, buf) != LDAP_SUCCESS)
	{
		free(buf);
		free(p->base);
		free(p);
		return NULL;
	}
	free(buf);

	if (userid && *userid)
	{
		struct berval cred;

		cred.bv_len=password && *password ? strlen(password):0;
		cred.bv_val=password && *password ? (char *)password:NULL;

		if (ldap_sasl_bind_s(p->handle, userid, NULL, &cred, NULL,
				     NULL, NULL))
		{
			l_search_free(p);
			errno=EPERM;
			return NULL;
		}
	}

	return p;
}

void l_search_free(struct ldapsearch *s)
{
	if (s->handle)
		ldap_unbind_ext(s->handle, NULL, NULL);
	free(s->base);
	free(s);
}

/*
** See RFC 2254 section 4.
*/

static char *encode_key(const char *lookupkey)
{
	const char *cp;

	char *p=NULL, *q;
	int pass;
	size_t l=0;

	for (pass=0; pass<2; pass++)
	{
		if (pass)
		{
			p=malloc(l);
			if (!p)
				return NULL;
		}
		l=1;
		q=p;
		for (cp=lookupkey; *cp; cp++)
		{
			const char *h;

			switch (*cp) {
			case '*':
				h="\\2a";
				break;
			case '(':
				h="\\28";
				break;
			case ')':
				h="\\29";
				break;
			case '\\':
				h="\\5c";
				break;
			default:
				if (pass)
					*q++= *cp;
				++l;
				continue;
			}

			if (pass)
				while ((*q++ = *h++) != 0)
					;
			l += 3;
		}
		if (pass)
			*q=0;
	}
	return p;
}

/*
** Insert lookup key into the search filter.
*/

static char *make_search_key(const char *filter, const char *lookupkey)
{
	size_t l=strlen(filter)+1;
	char *p, *q;
	const char *cp;

	for (cp=filter; *cp; cp++)
		if (*cp == '@')
			l += strlen(lookupkey);

	p=malloc(l);
	if (!p)
		return NULL;

	for (q=p, cp=filter; *cp; cp++)
	{
		if (*cp == '@')
		{
			const char *k=lookupkey;

			while ( (*q++ = *k++ ) != 0)
				;
			--q;
			continue;
		}
		*q++ = *cp;
	}
	*q=0;
	return p;
}

static int l_search_do_filter(struct ldapsearch *s,

			      int (*callback_func)(const char *utf8_name,
						   const char *address,
						   void *callback_arg),
			      void *callback_arg,

			      const char *filter,
			      const char *lookup_key,
			      int *found);


int l_search_do(struct ldapsearch *s,
		const char *lookupkey,

		int (*callback_func)(const char *utf8_name,
				     const char *address,
				     void *callback_arg),
		void *callback_arg)
{
	char *k;
	const char *filter;
	int rc_code;
	int found;

	k=encode_key(lookupkey);
	if (!k)
		return -1;

	filter=getenv("LDAP_SEARCH_FILTER_EXACT");
	if (!filter)
		filter="(|(uid=@)(sn=@)(cn=@))";

	rc_code=l_search_do_filter(s, callback_func, callback_arg,
				   filter, k, &found);

	if (rc_code == 0 && !found)
	{
		filter=getenv("LDAP_SEARCH_FILTER_APPROX");
		if (!filter)
			filter="(|(uid=@*)(sn=@*)(mail=@*)(cn=@*))";

		rc_code=l_search_do_filter(s, callback_func, callback_arg,
					   filter, k, &found);
	}
	free(k);
	return rc_code;
}

static int l_search_do_filter(struct ldapsearch *s,
			      int (*callback_func)(const char *utf8_name,
						   const char *address,
						   void *callback_arg),
			      void *callback_arg,
			      const char *filter,
			      const char *lookup_key,
			      int *found)
{
	char *kk;
	struct timeval tv;
	LDAPMessage *result;
	char *attrs[3];
	int rc_code=0;
	int msgidp;

	*found=0;

	kk=make_search_key(filter, lookup_key);

	if (!kk)
		return -1;

	if (s->handle == NULL)
	{
		errno=ETIMEDOUT;  /* Timeout previously */
		return -1;
	}


	attrs[0]="cn";
	attrs[1]="mail";
	attrs[2]=NULL;

	tv.tv_sec=60*60;
	tv.tv_usec=0;

	if (ldap_search_ext(s->handle, s->base, LDAP_SCOPE_SUBTREE,
			    kk, attrs, 0, NULL, NULL, &tv, 1000000, &msgidp)
	    != LDAP_SUCCESS)
		return -1;

	do
	{
		int rc;
		LDAPMessage *msg;

		const char *timeout=getenv("LDAP_SEARCH_TIMEOUT");

		tv.tv_sec=atoi(timeout ? timeout:"30");
		tv.tv_usec=0;

		rc=ldap_result(s->handle, msgidp, 0, &tv, &result);

		if (rc <= 0)
		{
			if (rc == 0)
				errno=ETIMEDOUT;

			ldap_unbind_ext(s->handle, NULL, NULL);
			s->handle=NULL;
			rc_code= -1;
			break;
		}

		if (rc == LDAP_RES_SEARCH_RESULT)
		{
			ldap_msgfree(result);
			break; /* End of search */
		}

		if (rc != LDAP_RES_SEARCH_ENTRY)
		{
			ldap_msgfree(result);
			continue;
		}

		for (msg=ldap_first_message(s->handle, result); msg;
		     msg=ldap_next_message(s->handle, msg))
		{
			struct berval **n_val=
				ldap_get_values_len(s->handle, msg, "cn");
			struct berval **a_val=
				ldap_get_values_len(s->handle, msg, "mail");

			if (n_val && a_val)
			{
				size_t i, j;

				for (i=0; n_val[i]; i++)
					for (j=0; a_val[j]; j++)
					{
						char *p=malloc(n_val[i]->bv_len
							       +1);
						char *q=malloc(a_val[j]->bv_len
							       +1);

						if (!p || !q)
						{
							if (p) free(p);
							if (q) free(q);
							rc_code= -1;
							break;
						}

						memcpy(p, n_val[i]->bv_val,
						       n_val[i]->bv_len);
						p[n_val[i]->bv_len]=0;

						memcpy(q, a_val[j]->bv_val,
						       a_val[j]->bv_len);
						q[a_val[j]->bv_len]=0;

						rc_code=(*callback_func)
							(p, q, callback_arg);
						free(p);
						free(q);
						if (rc_code)
							break;
						*found=1;
					}
			}
			if (n_val)
				ldap_value_free_len(n_val);
			if (a_val)
				ldap_value_free_len(a_val);
		}

		ldap_msgfree(result);
	} while (rc_code == 0);
	return rc_code;
}

int l_search_ping(struct ldapsearch *s)
{
	char *attrs[2];
	struct timeval tv;
	LDAPMessage *result;
	int rc;
	int msgid;

	if (s->handle == NULL)
	{
		errno=ETIMEDOUT;  /* Timeout previously */
		return -1;
	}

	attrs[0]="objectClass";
	attrs[1]=NULL;

	tv.tv_sec=60*60;
	tv.tv_usec=0;

	if (ldap_search_ext(s->handle, s->base, LDAP_SCOPE_BASE,
			    "objectClass=*", attrs, 0, NULL, NULL, &tv,
			    1000000, &msgid) < 0)
		return -1;

	do
	{
		const char *timeout=getenv("LDAP_SEARCH_TIMEOUT");

		tv.tv_sec=atoi(timeout ? timeout:"30");
		tv.tv_usec=0;

		rc=ldap_result(s->handle, msgid, 0, &tv, &result);

		if (rc <= 0)
		{
			if (rc == 0)
				errno=ETIMEDOUT;

			ldap_unbind_ext(s->handle, NULL, NULL);
			s->handle=NULL;

			return -1;
		}

		ldap_msgfree(result);
	} while (rc != LDAP_RES_SEARCH_RESULT);
	return 0;
}

#if 0

#include <stdio.h>

static int cb(const char *utf8_name,
	      const char *address,
	      void *callback_arg)
{
	printf("\"%s\" <%s>\n", utf8_name, address);
	return 0;
}

int main(int argc, char **argv)
{
	struct ldapsearch *s=l_search_alloc("localhost", 389,
					    "dc=courier-mta,dc=com");
	int n;

	if (!s)
	{
		perror("l_search_alloc");
		exit(1);
	}

	for (n=1; n<argc; n++)
	{
		if (l_search_do(s, argv[n], cb, NULL) != 0)
		{
			printf("l_search_do: error\n");
		}
	}
	l_search_free(s);
	return 0;
}
#endif
