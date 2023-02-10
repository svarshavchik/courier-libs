#include "config.h"
#include	"buffer.h"
#include	"funcs.h"
#include	<string.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<math.h>


void	add_integer(std::string &b, unsigned long n)
{
	char	tbuf[40];
	char	*p=tbuf+sizeof(tbuf)-1;

	*p=0;
	do
	{
		*--p= (n % 10) + '0';
	} while ( (n /= 10) != 0);

	b += p;
}

void	add_number(std::string &buf, double d)
{
	char	tbuf[MAXLONGSIZE < 40 ? 40:MAXLONGSIZE+4];

	sprintf(tbuf, "%1.*g", MAXLONGSIZE, d);
	buf += (tbuf);
}

int	extract_int(const std::string &buf, const char *def)
{
	const	char *p=buf.c_str();
	auto	l=buf.size();
	int	minus=0;
	unsigned num=0;

	while (l && isspace(*p))
	{
		--l;
		++p;
	}

	if ((!l || (*p != '-' && (*p < '0' || *p > '9'))) && def != 0)
	{
		p=def;
		l=strlen(def);
	}

	if (l && *p == '-')
	{
		minus=1;
		--l;
		++p;
	}

	while (l)
	{
		if (*p < '0' || *p > '9')	break;
		num = num * 10 + (*p-'0');
		++p;
		--l;
	}
	if (minus)	num= -num;
	return ( (int)num );
}
