#include "html.h"

#include <stdio.h>
#include <string.h>

static void write_stdout(const unicode_char *uc, size_t n, void *dummy)
{
	while (n)
	{
		putchar(*uc++);
		--n;
	}
}

static char *cid_func(const char *cid, void *dummy)
{
	return strdup(cid);
}

int main(int argc, char **argv)
{
	struct htmlfilter_info *p;
	char buf[1024];
	unicode_char ubuf[1024];
	size_t n;

	p=htmlfilter_alloc(write_stdout, NULL);

	htmlfilter_set_http_prefix(p, "http://redirect?");
	htmlfilter_set_mailto_prefix(p, "http://mailto?");
	htmlfilter_set_convertcid(p, cid_func, NULL);

	while (fgets(buf, sizeof(buf), stdin) != NULL)
	{
		size_t i;

		n=strlen(buf);

		for (i=0; i<n; i++)
			ubuf[i]=buf[i];

		htmlfilter(p, ubuf, i);
	}
	htmlfilter_free(p);
	return 0;
}
