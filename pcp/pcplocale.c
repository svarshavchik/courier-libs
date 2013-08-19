/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include "pcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#include "pcp.h"

const char *pcp_am()
{
	char *am_buf=0;
	time_t t;
	struct tm *tmptr;
	char tim_buf[40];
	int n;

	if (am_buf)
		return (am_buf);

	time(&t);
	tmptr=localtime(&t);
	tmptr->tm_hour=6;

	n=strftime(tim_buf, sizeof(tim_buf), "%p", tmptr);
	tim_buf[n]=0;
	am_buf=strdup(tim_buf);
	return (am_buf);
}

const char *pcp_pm()
{
	char *am_buf=0;
	time_t t;
	struct tm *tmptr;
	char tim_buf[40];
	int n;

	if (am_buf)
		return (am_buf);

	time(&t);
	tmptr=localtime(&t);
	tmptr->tm_hour=18;

	n=strftime(tim_buf, sizeof(tim_buf), "%p", tmptr);
	tim_buf[n]=0;
	am_buf=strdup(tim_buf);
	return (am_buf);
}

static const char *wday_func(unsigned n, const char *fmt)
{
	static char buf[40];
	time_t t;
	struct tm *tmptr;
	int dir=0;

	if (n >= 7)
		return ("");

	time(&t);
	tmptr=localtime(&t);

	while (tmptr->tm_wday != n)
	{
		if (dir == 0)
			dir= tmptr->tm_mday > 15 ? -1:1;

		if (dir < 0)
		{
			tmptr->tm_mday--;
			tmptr->tm_wday= (tmptr->tm_wday + 6) % 7;
		}
		else
		{
			tmptr->tm_mday++;
			tmptr->tm_wday= (tmptr->tm_wday + 1) % 7;
		}
	}

	dir=strftime(buf, sizeof(buf), fmt, tmptr);
	buf[dir]=0;
	return (buf);
}

const char *pcp_wdayname(unsigned n)
{
	return (wday_func(n, "%a"));
}

const char *pcp_wdayname_long(unsigned n)
{
	return (wday_func(n, "%A"));
}

int pcp_wday(const char *p)
{
	int i;

	for (i=0; i<7; i++)
	{
		const char *q;

		if (strcasecmp(p, pcp_wdayname(i)) == 0)
			return (i);

		q=pcp_wdayname_long(i);

		if (strncasecmp(p, q, strlen(q)) == 0)
			return (i);
	}
	return (-1);
}

static const char *month_func(unsigned n, const char *fmt)
{
	static char buf[80];
	time_t t;
	struct tm *tmptr;
	int dir=0;

	if (n >= 12)
		return ("");

	time(&t);
	tmptr=localtime(&t);

	tmptr->tm_mday=1;
	tmptr->tm_mon=n;
	tmptr->tm_hour=12;
	tmptr->tm_min=0;
	tmptr->tm_sec=0;

	dir=strftime(buf, sizeof(buf), fmt, tmptr);
	buf[dir]=0;
	return (buf);
}

const char *pcp_monthname(unsigned n)
{
	return (month_func(n, "%b"));
}

const char *pcp_monthname_long(unsigned n)
{
	return (month_func(n, "%B"));
}

int pcp_month(const char *p)
{
	int i;

	for (i=0; i<12; i++)
	{
		const char *q;

		if (strcasecmp(p, pcp_monthname(i)) == 0)
			return (i);

		q=pcp_monthname_long(i);

		if (strncasecmp(p, q, strlen(q)) == 0)
			return (i);
	}
	return (-1);
}

static int is_digit(const char *p)
{
	while (*p)
	{
		if (!isdigit((int)(unsigned char)*p))
			return (0);
		++p;
	}
	return (1);
}

static int fix_year(int year)
{
	time_t now=time(NULL);
	int nn;

	if (year < 100)
	{
		struct tm *tmptr;

		tmptr=localtime(&now);

		nn=tmptr->tm_year + 1900;

		if (year >= (nn % 100))
			year += (nn / 100 * 100);
		else
			year += (nn / 100 * 100) + 100;
	}
	return (year);
}

static int get_year(int mon, int day)
{
	time_t t=time(NULL);
	struct tm *tmptr=localtime(&t);
	int m=tmptr->tm_mon+1;

	int year=tmptr->tm_year + 1900;

	if (m > mon || (m == mon && tmptr->tm_mday > day))
		++year;

	return (year);
}

static int scan_mdy(int sscan_rc, int *mon, int *day, int *year)
{
	time_t t;
	struct tm *tmptr;

	switch (sscan_rc) {
	case 0:
		return (-1);
	case 1:
		return (-1);
	case 2:
		time(&t);
		tmptr=localtime(&t);

		*year=tmptr->tm_year + 1900;

		if (*mon < tmptr->tm_mon+1 ||
		    (*mon == tmptr->tm_mon+1 && *day < tmptr->tm_mday))
			++ *year;
		break;
	case 3:
		if (*year < 0)
			return (-1);
		*year=fix_year(*year);
		break;
	}
	if (*mon <= 0 || *day <= 0)
		return (-1);
	return (0);
}

time_t pcp_parse_datetime(int *argn,
			  int argc,
			  char **argv,
			  struct pcp_parse_datetime_info *info)
{
	int mon=-1, day=-1, year=0;
	int hour=0, min=0, sec=-1;
	time_t t;
	struct tm *tmptr, tmsave;
	const char *today="today";
	const char *tomorrow="tomorrow";
	int nn;
	int i;

	time(&t);

	if (info && info->today_name)
		today=info->today_name;

	if (info && info->tomorrow_name)
		tomorrow=info->tomorrow_name;

	while (year == 0 || sec < 0 || day < 0)
	{
		const char *p;
		const char *q;

		if (*argn >= argc)
			break;

		p=argv[*argn];

		if (strcasecmp(p, today) == 0)
		{
			if (year || day >= 0)
				return (0);	/* Already specified */

			tmptr=localtime(&t);

			mon=tmptr->tm_mon+1;
			day=tmptr->tm_mday;
			year=tmptr->tm_year + 1900;
			++ *argn;
			continue;
		}

		if (strcasecmp(p, tomorrow) == 0)
		{
			time_t t2;

			if (year || day >= 0)
				return (0);	/* Already specified */

			tmptr=localtime(&t);

			tmptr->tm_hour=12;
			tmptr->tm_min=0;
			tmptr->tm_sec=0;

			t2=mktime(tmptr);
			if (t2 == (time_t)-1)
				return (0);

			t2 += 24 * 60 * 60;

			tmptr=localtime(&t2);

			mon=tmptr->tm_mon+1;
			day=tmptr->tm_mday;
			year=tmptr->tm_year + 1900;
			++ *argn;
			continue;
		}

		if ((i=pcp_wday(p)) >= 0)
		{
			time_t t2;

			if (year || day >= 0)
				return (0);	/* Already specified */

			t2=t;

			do
			{
				tmptr=localtime(&t2);

				tmptr->tm_hour=12;
				tmptr->tm_min=0;
				tmptr->tm_sec=0;

				t2=mktime(tmptr);
				if (t2 == (time_t)-1)
					return (0);

				t2 += 24 * 60 * 60;

				tmptr=localtime(&t2);
			} while (tmptr->tm_wday != i);

			mon=tmptr->tm_mon+1;
			day=tmptr->tm_mday;
			year=tmptr->tm_year + 1900;
			++ *argn;
			continue;
		}

		if ((i=pcp_month(p)) >= 0)
		{
			if (mon >= 0)
				return (0);
			mon=i+1;

			if (++*argn >= argc)
				return (0);
			day=atoi(argv[*argn]);
			if (day <= 0)
				return (0);
			++*argn;
			continue;
		}

		if (strchr(p, '/'))
		{
			if (mon > 0 || day > 0 || year > 0)
				return (0);

			if (scan_mdy(sscanf(p, "%d/%d/%d", &mon, &day, &year),
				     &mon, &day, &year))
				return (0);
			++*argn;
			continue;
		}

		if (strchr(p, '.'))
		{
			if (mon > 0 || day > 0 || year > 0)
				return (0);

			if (scan_mdy(sscanf(p, "%d.%d.%d", &year, &mon, &day),
				     &mon, &day, &year))
				return (0);
			++*argn;
			continue;
		}

		if (is_digit(p) && year == 0 &&
		    mon > 0 && day > 0 && sec < 0)
		{
			year=atoi(p);
			year=fix_year(year);
			++*argn;
			continue;
		}

		nn=atoi(p);

		if (is_digit(p))
		{
			++*argn;

			if (*argn >= argc)
				return (0);

			p=argv[*argn];

			for (i=0; i<12; i++)
				if (strcasecmp(p, pcp_monthname(i)) == 0 ||
				    strcasecmp(p, pcp_monthname_long(i)) == 0)
					break;

			if (i < 12)
			{
				if (nn == 0)
					return (0);

				if (mon >= 0)
					return (0);
				mon=i+1;
				day=nn;
				++*argn;
				continue;
			}

			if (strcasecmp(p, pcp_am()) == 0)
			{
				if (hour >= 0)
					return (0);
				hour=nn;
				min=0;
				sec=0;
				++*argn;
				if (hour == 12)
					hour=0;

				continue;
			}

			if (strcasecmp(p, pcp_pm()) == 0)
			{
				if (hour >= 0)
					return (0);
				hour=nn+12;
				min=0;
				sec=0;
				++*argn;
				if (hour == 24)
					hour=12;
				continue;
			}

			if (hour >= 0)
				return (0);
			hour=nn;
			min=0;
			sec=0;
			continue;
		}

		if (!isdigit((int)(unsigned char)*p))
			break;

		if (mon > 0 && day > 0 && year == 0)
		{
			year=get_year(mon, day);
		}


		switch (sscanf(p, "%d:%d:%d", &hour, &min, &sec)) {
		case 0:
			return (0);
		case 1:
			if (sec >= 0)
				return (0);

			min=sec=0;
			break;
		case 2:
			if (sec >= 0)
				return (0);

			sec=0;
			break;
		default:
			if (sec >= 0)
				return (0);
			break;
		}

		for (q=p; *q; q++)
		{
			if (!isdigit((int)(unsigned char)*q) && *q != ':')
				break;
		}

		++*argn;

		if (*q == 0)
		{
			q= *argn < argc ? argv[*argn]:"";
			if (strcasecmp(p, pcp_am()) == 0)
			{
				if (hour == 12)
					hour=0;
				++*argn;
				continue;
			}
			if (strcasecmp(p, pcp_pm()) == 0)
			{
				hour += 12;
				if (hour == 24)
					hour=12;
				++*argn;
				continue;
			}
			continue;
		}

		if (strcasecmp(q, pcp_am()) == 0)
		{
			if (hour == 12)
				hour=0;
			continue;
		}
		if (strcasecmp(q, pcp_pm()) == 0)
		{
			hour += 12;
			if (hour == 24)
				hour=12;
			continue;
		}
		return (0);
	}

	if (sec < 0)
	{
		hour=min=sec=0;
	}

	if (day <= 0 && mon < 0 && year <= 0)
	{
		tmptr=localtime(&t);

		mon=tmptr->tm_mon+1;
		day=tmptr->tm_mday;
		year=tmptr->tm_year + 1900;
	}
	if (day < 0)
		return (0);

	tmptr=localtime(&t);
	tmptr->tm_mon=mon-1;
	tmptr->tm_mday=day;
	tmptr->tm_year=year - 1900;
	tmptr->tm_hour=hour;
	tmptr->tm_min=min;
	tmptr->tm_sec=sec;

	tmsave= *tmptr;

	t=mktime(tmptr);
	if (t == (time_t)-1)
		return (0);

	/* Could be different due to altzone change, so we do it again */

	tmptr=localtime(&t);
	tmptr->tm_mon=tmsave.tm_mon;
	tmptr->tm_mday=tmsave.tm_mday;
	tmptr->tm_year=tmsave.tm_year;
	tmptr->tm_hour=tmsave.tm_hour;
	tmptr->tm_min=tmsave.tm_min;
	tmptr->tm_sec=tmsave.tm_sec;
	t=mktime(tmptr);
	if (t == (time_t)-1)
		return (0);
	return (t);
}

static time_t add_week(time_t n, unsigned cnt)
{
	struct tm *tmptr=localtime(&n);
	struct tm tmsave= *tmptr;

	if (!tmptr)
		return (0);

	tmptr->tm_hour=13;
	tmptr->tm_min=0;
	tmptr->tm_sec=0;

	n=mktime(tmptr);

	if (n == (time_t)-1)
		return (0);

	n += cnt * 7 * 24 * 60 * 60;

	tmptr=localtime(&n);

	tmptr->tm_hour=tmsave.tm_hour;
	tmptr->tm_min=tmsave.tm_min;
	tmptr->tm_sec=tmsave.tm_sec;

	n=mktime(tmptr);
	if (n == (time_t)-1)
		return (0);
	return (n);
}

static time_t add_month(time_t n, unsigned cnt)
{
	time_t a, b;

	struct tm *tmptr=localtime(&n);
	int mday, d, m, y, hh, mm, ss;
	int is_eom=0;
	int i;

	if (!tmptr)
		return (0);

	mday=d=tmptr->tm_mday;
	m=tmptr->tm_mon+1;
	y=tmptr->tm_year + 1900;
	hh=tmptr->tm_hour;
	mm=tmptr->tm_min;
	ss=tmptr->tm_sec;

	if (pcp_parse_ymd(y, m, d, &a, &b))
		return (0);

	tmptr=localtime(&b);
	if (!tmptr)
		return (0);

	if (tmptr->tm_mday == 1)	/* The original date is end of month */
		is_eom=1;

	/* Go to the next month, then backtrack */

	++cnt;

	y += (cnt / 12);
	m += cnt % 12;

	if (m > 12)
	{
		m -= 12;
		++y;
	}

	tmptr->tm_mday=1;
	tmptr->tm_mon=m-1;
	tmptr->tm_year= y - 1900;
	tmptr->tm_hour=13;
	tmptr->tm_min=0;
	tmptr->tm_sec=0;

	if ((a=mktime(tmptr)) == (time_t)-1)
		return (0);

	a -= 24 * 60 * 60;

	for (i=0; i<32; i++)	/* Stopgap */
	{
		tmptr=localtime(&a);
		if (!tmptr)
			return (0);

		if (is_eom || tmptr->tm_mday <= mday)
			break;

		tmptr->tm_hour=13;
		tmptr->tm_min=0;
		tmptr->tm_sec=0;
		if ((a=mktime(tmptr)) == (time_t)-1)
			return (0);

		a -= 24 * 60 * 60;
	}

	tmptr->tm_hour=hh;
	tmptr->tm_min=mm;
	tmptr->tm_sec=ss;

	if ((a=mktime(tmptr)) == (time_t)-1
	    || (tmptr=localtime(&a)) == NULL)
		return (0);

	tmptr->tm_hour=hh;
	tmptr->tm_min=mm;
	tmptr->tm_sec=ss;

	if ((a=mktime(tmptr)) == (time_t)-1)
		return (0);

	return (a);
}

int pcp_parse_datetime_until(time_t start, time_t end,
			     int *argn,
			     int argc,
			     char **argv,

			     int recurring_type,

			     int (*save_date_time)(time_t, time_t, void *),
			     void *voidfunc)
{
	int month=0, day=0, year=0;
	const char *p;
	struct tm *tmptr;

	time_t new_start, new_end;
	unsigned cnt;

	while (month == 0 || day == 0)
	{
		if (*argn >= argc)
			return (-1);

		p=argv[ (*argn)++ ];

		if (month == 0 && day == 0 && year == 0)
		{
			if (strchr(p, '/'))
			{
				if (scan_mdy(sscanf(p, "%d/%d/%d",
						    &month, &day, &year),
					     &month, &day, &year))
					return (-1);
				continue;
			}

			if (strchr(p, '.'))
			{
				if (scan_mdy(sscanf(p, "%d.%d.%d",
						    &year, &month, &day),
					     &month, &day, &year))
					return (-1);
				continue;
			}
		}

		if (is_digit(p))
		{
			if (day > 0)
				return (-1);
			day=atoi(p);
			if (day <= 0)
				return (-1);
			continue;
		}

		if (month > 0)
			return (-1);

		month=pcp_month(p);

		if (month < 0)
			return (-1);
		++month;
	}

	if ( year == 0 && *argn < argc && is_digit(argv[*argn]))
	{
		year=atoi(argv[*argn++]);

		year=fix_year(year);
	}
	else
	{
		if (year == 0)
			year=get_year(month, day);
	}

	for (cnt=0; cnt < 100; cnt++)
	{
		int y, m;

		switch (recurring_type) {
		case PCP_RECURRING_MONTHLY:
			new_start=add_month(start, cnt);
			new_end=add_month(end, cnt);
			break;
		case PCP_RECURRING_ANNUALLY:
			new_start=add_month(start, cnt*12);
			new_end=add_month(end, cnt*12);
			break;
		default:
			new_start=add_week(start, cnt);
			new_end=add_week(end, cnt);
		}

		if (!new_start || !new_end)
			continue;

		tmptr=localtime(&new_start);

		y=tmptr->tm_year + 1900;
		m=tmptr->tm_mon + 1;

		if (y > year)
			break;
		if (y == year)
		{
			if (m > month)
				break;
			if (m == month)
			{
				if (tmptr->tm_mday > day)
					break;
			}
		}

		y= (*save_date_time)(new_start, new_end, voidfunc);
		if (y)
			return (y);
	}
	return (0);
}

int pcp_fmttime(char *ubuf, size_t cnt, time_t t, int flags)
{
	struct tm *tmptr=localtime(&t);
	char datebuf[100];
	char timebuf[100];
	char buf[201];

	char *p;

	if (!tmptr)
		return (-1);

	if (strftime(datebuf, sizeof(datebuf), "%x", tmptr) <= 0)
		datebuf[0]=0;

	if (strftime(timebuf, sizeof(timebuf), "%X", tmptr) <= 0)
		timebuf[0]=0;

	if (tmptr->tm_hour == 0 && tmptr->tm_min == 0 && tmptr->tm_sec == 0
	    && (flags & FMTTIME_TIMEDROP))
		timebuf[0]=0;

	/* Try to drop minutes from timebuf */

	for (p=timebuf; *p; p++)
	{
		if (!isdigit((int)(unsigned char)*p) &&
		    p[1] == '0' && p[2] == '0')
		{
			char *q;

			for (q=p+3; *q; q++)
				if (isdigit((int)(unsigned char)*q))
				    break;

			if (!*q)
			{
				for (q=p+3; (*p=*q) != 0; p++, q++)
					;
				break;
			}
		}
	}

	buf[0]=0;
	if (!flags || (flags & FMTTIME_DATE))
	{
		strcpy(buf, datebuf);
	}

	if (!flags || (flags & FMTTIME_TIME))
	{
		if (timebuf[0])
		{
			if (buf[0])
				strcat(buf, " ");
			strcat(buf, timebuf);
		}
	}

	if (strlen(buf)+1 > cnt)
	{
		errno=ENOSPC;
		return (-1);
	}

	strcpy(ubuf, buf);
	return (0);
}

int pcp_fmttimerange(char *ubuf, size_t cnt, time_t from, time_t to)
{
	char date1[100];
	char time1[100];
	char date2[100];
	char time2[100];
	char fmtbuf[500];

	if (pcp_fmttime(date1, sizeof(date1), from, FMTTIME_DATE) < 0)
		date1[0]=0;

	if (pcp_fmttime(time1, sizeof(time1), from,
			FMTTIME_TIME | FMTTIME_TIMEDROP) < 0)
		time1[0]=0;

	if (pcp_fmttime(date2, sizeof(date2), to, FMTTIME_DATE) < 0)
		date2[0]=0;
	if (pcp_fmttime(time2, sizeof(time2), to,
			FMTTIME_TIME | FMTTIME_TIMEDROP) < 0)
		time2[0]=0;

	if (time1[0] == 0 && time2[0] == 0)
	{
		sprintf(fmtbuf, strcmp(date1, date2)
			? "%s-%s":"%s", date1, date2);
	}
	else
	{
		if (!time1[0] && pcp_fmttime(time1, sizeof(time1), from,
					     FMTTIME_TIME) < 0)
			time1[0]=0;

		if (!time2[0] && pcp_fmttime(time2, sizeof(time2), to,
					     FMTTIME_TIME) < 0)
			time2[0]=0;

		if (strcmp(date1, date2) == 0)
			sprintf(fmtbuf, "%s %s-%s", date1, time1, time2);
		else
			sprintf(fmtbuf, "%s %s-%s %s", date1, time1,
				date2, time2);
	}

	if (strlen(fmtbuf)+1 > cnt)
	{
		errno=ENOSPC;
		return (-1);
	}

	strcpy(ubuf, fmtbuf);
	return (0);
}
