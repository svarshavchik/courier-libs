#include "config.h"
#include	"buffer.h"
#include	"funcs.h"
#include	<string.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<math.h>


#define	CHUNK	512

void	Buffer::append(int c)
{
int	newsize=bufsize+CHUNK;
unsigned char	*newbuf=new unsigned char[newsize];

	if (!newbuf)	outofmem();
	if (bufsize)	memcpy(newbuf, buf, bufsize);
	if (buf)	delete[] buf;
	buf=newbuf;
	bufsize = newsize;
	buf[buflength++]=c;
}

void	Buffer::set(const char *p)
{
int	l=strlen(p);

	if (bufsize < l)
	{
	int	newsize=l + CHUNK-1;

		newsize -= (newsize % CHUNK);
	unsigned char	*newbuf=new unsigned char[newsize];

		if (!newbuf)	outofmem();
		if (buf)	delete[] buf;
		buf=newbuf;
		bufsize=newsize;
	}
	memcpy(buf, p, l);
	buflength=l;
}

int	Buffer::operator-(const Buffer &o) const
{
int	i;

	for (i=0; i<buflength && i < o.buflength; i++)
	{
		if (buf[i] < o.buf[i])	return (-1);
		if (buf[i] > o.buf[i])	return (1);
	}
	return (buflength < o.buflength ? -1:
		buflength > o.buflength ? 1:0);
}

int	Buffer::operator-(const char *o) const
{
int	i;

	for (i=0; i<buflength && o[i]; i++)
	{
		if (buf[i] < o[i])	return (-1);
		if (buf[i] > o[i])	return (1);
	}
	return (i < buflength ? 1: o[i] ? -1:0);
}

Buffer &Buffer::operator=(const Buffer &o)
{
	if (bufsize < o.buflength)
	{
	int	newsize=(o.buflength + CHUNK-1);

		newsize -= (newsize % CHUNK);
	unsigned char	*newbuf=new unsigned char[newsize];

		if (!newbuf)	outofmem();
		if (buf)	delete[] buf;
		buf=newbuf;
		bufsize=newsize;
	}
	if (o.buflength) memcpy(buf, o.buf, o.buflength);
	buflength=o.buflength;
	return (*this);
}

void	Buffer::append(const void *p, int l)
{
	if (bufsize - buflength < l)
	{
	int	newsize=(buflength+l + CHUNK-1);

		newsize -= (newsize % CHUNK);
	unsigned char	*newbuf=new unsigned char[newsize];

		if (!newbuf)	outofmem();
		if (buf)
		{
			memcpy(newbuf, buf, buflength);
			delete[] buf;
		}
		buf=newbuf;
		bufsize=newsize;
	}
	if (l) memcpy(buf+buflength, p, l);
	buflength += l;
}

void	Buffer::append(unsigned long n)
{
char	tbuf[40];
char	*p=tbuf+sizeof(tbuf)-1;

	*p=0;
	do
	{
		*--p= (n % 10) + '0';
	} while ( (n /= 10) != 0);
	append(p);
}


void	Buffer::append(double d)
{
char	tbuf[MAXLONGSIZE < 40 ? 40:MAXLONGSIZE+4];

	sprintf(tbuf, "%1.*g", MAXLONGSIZE, d);
	operator += (tbuf);
}

int	Buffer::Int(const char *def) const
{
const	unsigned char *p=buf;
int	l=buflength;
int	minus=0;
unsigned num=0;

	while (l && isspace(*p))
	{
		--l;
		++p;
	}

	if ((!l || (*p != '-' && (*p < '0' || *p > '9'))) && def != 0)
	{
		p= (const unsigned char *)def;
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
