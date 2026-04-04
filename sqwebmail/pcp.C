/*
** Copyright 2001-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"pcp.h"
#include	"pref.h"
#include	"htmllibdir.h"
#include	"sqconfig.h"
#include	"auth.h"
#include	"addressbook.h"
#include	"pcp/pcp.h"
#include	"cgi/cgi.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	"rfc2045/rfc2045.h"
#include	"maildir/maildircreate.h"
#include	"maildir/maildirmisc.h"
#include	"maildir/maildirquota.h"
#include	"maildir/maildirgetquota.h"
#include	"numlib/numlib.h"
#include	"maildir.h"
#include	"newmsg.h"
#include	"pref.h"
#include	"courierauth.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<ctype.h>
#include	<unistd.h>
#include	<errno.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#if HAVE_SYS_WAIT_H
#include	<sys/wait.h>
#endif
#if HAVE_FCNTL_H
#include	<fcntl.h>
#endif
#include	<vector>
#include	<string_view>
#include	<charconv>
#include	<fstream>
#include	<unordered_set>
#include	<algorithm>
#include	<tuple>

#ifndef WEXITSTATUS
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif

#define		CACHE	"calendar.cache"
#define		TOKEN	"calendar.authtoken"

#define		CHANGEDFILE "calendar/changed"	/* Hack */

#include	"strftime.h"

extern "C" FILE *open_langform(const char *lang, const char *formname,
			       int print_header);

extern "C" const char *myhostname();
void output_attrencoded_oknl_fp(const char *, FILE *);
extern "C" void output_scriptptrget();
void output_attrencoded(const char *);
extern "C" void print_safe(const char *);
extern const char *sqwebmail_content_charset;
extern const char *sqwebmail_content_language;
extern "C" void output_form(const char *);
extern "C" void newmsg_preview(const char *);
extern "C" void output_urlencoded(const char *);
extern "C" void attachments_head(const char *, const char *, const char *);
extern std::string newmsg_createsentmsg(const char *, int *);
extern const char *sqwebmail_mailboxid;
extern "C" char *scriptptrget();
extern "C" void attach_delete(const char *);
extern "C" int attach_upload(const char *,
			     const char *,
			     const char *);

extern void newmsg_showfp(rfc822::fdstreambuf &fp, int *attachcnt);

static struct PCP *calendar=NULL;
static void refreshcache(struct PCP *);
extern "C" size_t get_timeoutsoft();

/*
** CGI process startup
*/

void sqpcp_init()
{
	calendar=NULL;
}

/*
** CGI process shutdown
*/

void sqpcp_close()
{
	if (calendar)
	{
		pcp_close(calendar);
		calendar=NULL;
	}
}

static int checked_calendar_mode=0;
static char calendar_mode[24];

static const char *sqpcp_mode()
{
	if (!checked_calendar_mode)
	{
		FILE *f;
		char *p;

		calendar_mode[0]=0;

		f=fopen(CALENDARMODE, "r");

		if (f)
		{
			if (fgets(calendar_mode, sizeof(calendar_mode),
				  f) == NULL)
			{
				calendar_mode[0]=0;
			}
			fclose(f);
		}

		if ((p=strchr(calendar_mode, '\n')) != 0)
			*p=0;
		checked_calendar_mode=1;
	}

	return (calendar_mode[0] ? calendar_mode:NULL);
}

/*
** Log in to calendar.
*/

static void do_pcplogin(const char *userid, const char *password, int showerr);

void sqpcp_login(const char *userid, const char *password)
{
	do_pcplogin(userid, password, 0);
}

static void do_pcplogin(const char *userid, const char *password, int showerr)
{
	struct PCP *pcp;

	unlink(TOKEN);
	unlink(CACHE);	/* Rebuild it later*/

	if (!sqpcp_mode())
		return;

	mkdir("calendar", 0700);

	if (sqpcp_has_groupware())	/* Groupware mode, login to server */
	{
		char *errflag=0;

		pcp=pcp_open_server(userid, password, &errflag);

		if (!pcp && errflag && showerr)
		{
			printf("%s<div class=\"indent\"><pre class=\"small-error\">",
			       getarg("CALENDARLOGINERR"));
			output_attrencoded_oknl_fp(errflag, stdout);
			printf("</pre></div>\n");
		}
		if (errflag)
			free(errflag);
	}
	else
	{
		pcp=sqpcp_calendar();
	}

	if (pcp)
	{
		const char *p=pcp_authtoken(pcp);

		if (p)
		{
			FILE *f;
			int u=umask(077);

			f=fopen(TOKEN, "w");
			umask(u);
			if (f)
			{
				fprintf(f, "%s\n", p);
				fclose(f);
			}
		}
		pcp_cleanup(pcp);
		refreshcache(pcp);
	}
}

/*
** Format of the CACHE file:
**
**  start<TAB>end<TAB>eventid<TAB>flags<TAB>subject
*/

struct cacherecord {
	time_t start, end;
	std::string eventid;
	std::string flags;
	std::string subject;

	bool operator<(const cacherecord &o) const
	{
		if (start < o.start) return true;
		if (o.start < start) return false;

		if (end < o.end) return true;
		if (o.end < end) return false;

		if (eventid < o.eventid) return true;
		return false;
	}
};

int sqpcp_has_calendar()
{
	return (sqpcp_mode() ? 1:0);
}

int sqpcp_has_groupware()
{
	const char *p=sqpcp_mode();

	return (p && strcmp(p, "net") == 0);
}

static bool createcache(struct PCP *,
			std::vector<cacherecord> &, time_t, time_t);

/*
** Are we logged in?  If so, refresh the calendar event cache that we
** display on the main folders screen, if it is stale.
*/

static int need_refresh();

int sqpcp_loggedin()
{
	struct PCP *pcp;

	if (!sqpcp_mode())
		return (0);

	if (sqpcp_has_groupware())
	{
		struct stat stat_buf;

		if (stat(TOKEN, &stat_buf))
			return (0);	/* Login session dropped */
	}

	if (!need_refresh())
		return (1);
	pcp=sqpcp_calendar();
	if (pcp)
		refreshcache(pcp);
	return (1);
}

/* Check if it's time to rebuild CACHE file */

static const char *parsetimet(const char *buf, time_t *tp)
{
	*tp=libmail_strtotime_t(&buf);

	if (*buf == '\t')
		++buf;
	return buf;
}

static int need_refresh()
{
	FILE *fp;
	struct stat stat_buf;

	/* Check whether it's time to rebuild the cache */

	fp=fopen(CACHE, "r");

	if (stat(CHANGEDFILE, &stat_buf) == 0)
	{
		if (fp)
			fclose(fp);
		return (1);
	}

	if (fp)
	{
		struct stat stat_buf;

		char buffer[BUFSIZ];
		const char *p;

		time_t a, b;

		int ch=getc(fp);

		if (ch == EOF)
		{
			fclose(fp);
			return (0); /* No events scheduled */
		}

		ungetc(ch, fp);
		if (fgets(buffer, sizeof(buffer), fp) &&
		    (p=parsetimet(buffer, &a)) &&
		    parsetimet(p, &b))
		{
			time_t now;

			time(&now);

			if (now < b)	/* Event expired */
			{
				if (fstat(fileno(fp), &stat_buf) == 0)
				{
					/*
					** Check in every TIMEOUTSOFT/2 in
					** any case.
					*/

					if (stat_buf.st_mtime > (time_t)(
						    now - get_timeoutsoft()/2))
					{
						fclose(fp);	/* Not yet */
						return (0);
					}
				}
			}
		}
		fclose(fp);
		unlink(CACHE);
		return (1);
	}
	return (0);
}

static void refreshcache(struct PCP *pcp)
{
	std::vector<cacherecord> recs;
	unsigned i;
	std::string new_name;
	int new_fd;
	time_t now;
	FILE *new_fp;

	unlink(CHANGEDFILE);
	time(&now);
	if (!createcache(pcp, recs, now, now + 5 * 24 * 60 * 60))
		return;

	new_fd=maildir_createmsg(INBOX, "cache", new_name);
	if (new_fd < 0 || (new_fp=fdopen(new_fd, "w")) == 0)
	{
		if (new_fd >= 0)        close(new_fd);
		return;
	}

	new_name.insert(0, "tmp/");

	/* Save a max of 5 events, for the main page listing */

	for (i=0; i<recs.size() && i < 5; i++)
	{
		char buf1[NUMBUFSIZE], buf2[NUMBUFSIZE];

		fprintf(new_fp, "%s\t%s\t%s\t%s\t%s\n",
			libmail_str_time_t(recs[i].start, buf1),
			libmail_str_time_t(recs[i].end, buf2),
			recs[i].eventid.c_str(),
			recs[i].flags.c_str(),
			recs[i].subject.c_str());
	}

	if (fflush(new_fp) || ferror(new_fp))
	{
		fclose(new_fp);
		unlink(new_name.c_str());
                return;
        }
	fclose(new_fp);
	rename(new_name.c_str(), CACHE);
	return;
}

static int callback_createcache(struct PCP_list_all *, void *);
static int callback_retr_status(struct PCP_retr *, int, void *);
static int callback_retr_headers(struct PCP_retr *, const char *,
				 const char *, void *);

static bool createcache(struct PCP *pcp,
			std::vector<cacherecord> &recs,
			time_t start, time_t end)
{
	struct PCP_list_all la;
	struct PCP_retr r;

	memset(&la, 0, sizeof(la));
	la.list_from=start;
	la.list_to=end;
	la.callback_func=callback_createcache;
	la.callback_arg= &recs;

	if (pcp_list_all(pcp, &la))
	{
		recs.clear();
		return true;
	}

	std::sort(recs.begin(), recs.end());

	if (recs.empty())
		return false;	/* Nothing */

	std::vector<char *> event_ids;
	event_ids.reserve(recs.size()+1);

	for (auto &r:recs)
		event_ids.push_back(r.eventid.data());

	event_ids.push_back(nullptr);

	memset(&r, 0, sizeof(r));
	r.event_id_list=event_ids.data();
	r.callback_arg=&recs;
	r.callback_retr_status=callback_retr_status;
	r.callback_headers_func=callback_retr_headers;

	if (pcp_retr(pcp, &r))
	{
		fprintf(stderr, "NOTICE: pcp_retr: error: %s\n", pcp_errmsg(pcp));
		return (-1);
	}
	return (true);
}

/* Callback from PCP LIST - save the event ID in the cacherecord_list */

static int callback_createcache(struct PCP_list_all *a, void *vp)
{
	auto listp=reinterpret_cast<std::vector<cacherecord> *>(vp);

	listp->push_back(
		cacherecord{a->event_from, a->event_to,
			    a->event_id}
	);

	return (0);
}

static int callback_retr_status(struct PCP_retr *r, int s, void *vp)
{
	auto listp=reinterpret_cast<std::vector<cacherecord> *>(vp);
	char stat_buf[256];

	stat_buf[0]=0;

	if (s & LIST_CANCELLED)
	{
		strcat(stat_buf, " CANCELLED");
	}

	if (s & LIST_BOOKED)
	{
		strcat(stat_buf, " BOOKED");
	}

	auto p=stat_buf;
	if (*p)
		++p;

	for (auto &rec:*listp)
	{
		if (rec.eventid == r->event_id)
		{
			rec.flags=p;
			break;
		}
	}
	return (0);
}

/* RETR callback for headers - save the Subject: header */

static int callback_retr_headers(struct PCP_retr *r, const char *h,
				 const char *v, void *vp)
{
	auto listp=reinterpret_cast<std::vector<cacherecord> *>(vp);

	if (strcasecmp(h, "Subject") || !v)
		return (0);

	for (auto &rec:*listp)
	{
		if (rec.eventid == r->event_id)
		{
			rec.subject=v;
		}
	}

	return (0);
}

static void parsecache_rec(std::string_view line, struct cacherecord *cr)
{
	if (line.empty())
		return;

	auto p=line.data();
	auto e=p+line.size();

	auto ret=std::from_chars(p, e, cr->start);

	if (ret.ec != std::errc())
		return;
	p=std::find(p, e, '\t');
	if (p == e)
		return;
	++p;
	ret=std::from_chars(p, e, cr->end);
	if (ret.ec != std::errc())
		return;
	p=std::find(p, e, '\t');
	if (p == e)
		return;
	++p;

	auto q=std::find(p, e, '\t');
	cr->eventid=std::string(p, q);
	if (q == e)
		return;
	p= ++q;

	q=std::find(p, e, '\t');
	cr->flags=std::string(p, q);
	if (q == e)
		return;
	p= ++q;

	cr->subject=std::string(q, e);
}

struct PCP *sqpcp_calendar()
{
	const char *p;

	if (!sqpcp_mode())
	{
		errno=ENOENT;
		return (NULL);
	}

	if (calendar)
		return (calendar);

	p=getenv("AUTHADDR");

	if (!p)
	{
		errno=ENOENT;
		return (NULL);
	}

	if (sqpcp_has_groupware())
	{
		char token[256];
		char *pp;

		FILE *f=fopen(TOKEN, "r");

		if (!f)
			return (NULL);

		if (fgets(token, sizeof(token), f) == NULL)
		{
			fclose(f);
			return (NULL);
		}

		if ((pp=strchr(token, '\n')) != 0) *pp=0;

		fclose(f);
		if (token[0] == 0)
		{
			unlink(TOKEN);
			return (NULL);
		}

		calendar=pcp_reopen_server(p, token, NULL);

		if (calendar)
		{
			p=pcp_authtoken(calendar);

			if (p && strcmp(p, token))
			{
				int u=umask(077);

				/* Save new authentication token */

				f=fopen(TOKEN, "w");
				umask(u);
				if (f)
				{
					fprintf(f, "%s\n", token);
					fclose(f);
				}
			}
		}
		else
		{
			unlink(TOKEN);
		}
		return (calendar);
	}

	return ((calendar=pcp_open_dir("calendar", p)));
}


/* ---------------------------------------------------------------- */
/* Display event information */
/* ---------------------------------------------------------------- */

static std::vector<struct PCP_event_time> event_time_list;
static rfc822::fdstreambuf openoldfp(const char *p, unsigned long *prev_size);

struct my_participant {
	std::string name, address;
} ;

static std::vector<my_participant> my_participant_list;

static void add_my_participant(std::string_view participant)
{
	my_participant_list.push_back(
		{
			"",
			std::string{participant.begin(),
				participant.end()
			}
		}
	);
}

static bool parse_event_time(std::string_view value,
			     time_t &n1,
			     time_t &n2)
{
	auto e=value.data()+value.size();

	auto [ptr1, ec1] = std::from_chars(
		value.data(),
		e,
		n1
	);

	while (ptr1 != value.data()+value.size())
	{
		if (*ptr1 != ' ' && *ptr1 != '-')
			break;
		++ptr1;
	}

	const auto &[ptr2, ec2] = std::from_chars(
		ptr1,
		e,
		n2
	);

	return (ec1 == std::errc{} && ec2 == std::errc{});
}

static std::string from_buf, subj_buf;

void sqpcp_eventstart()
{
	const char *p=cgi("draftmessage");

	sqpcp_eventend();

	if (!p || !*p)
		return;

	auto oldfp=openoldfp(p, NULL);
	if (oldfp.error())
		return;

	rfc2045::entity::line_iter<false>::headers h{oldfp};

	event_time_list.clear();
	my_participant_list.clear();
	do
	{
		const auto &[header, value] = h.name_content();

		if (header == "x-event-time" && !value.empty())
		{
			time_t n1, n2;

			if (parse_event_time(value, n1, n2))
			{
				event_time_list.push_back(
					{
						n1,
						n2
					}
				);
			}
		}
		else if (header == "x-event-participant" && !value.empty())
		{
			add_my_participant(value);
		}
		else if (header == "from" && !value.empty())
		{
			from_buf.clear();

			rfc822::display_header(
				header,
				value,
				sqwebmail_content_charset,
				std::back_inserter(from_buf)
			);
			cgi_put("from", from_buf.c_str());
		}
		else if (header == "subject" && !value.empty())
		{
			subj_buf.clear();

			rfc822::display_header(
				header,
				value,
				sqwebmail_content_charset,
				std::back_inserter(subj_buf)
			);
			cgi_put("headersubject", subj_buf.c_str());
		}
	} while (h.next());

	std::sort(event_time_list.begin(),
		  event_time_list.end(),
		  [](const auto &a, const auto &b)
		  {
			  return (a.start < b.start);
		  });
}

void sqpcp_eventend()
{
	my_participant_list.clear();
	event_time_list.clear();
}

void sqpcp_eventtimes()
{
	char buffer[512];

	const char *sep="";
	for (auto &t:event_time_list)
	{
		printf("%s", sep);
		sep="<br />\n";
		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     t.start,
				     t.end))
			continue;
		printf("<span class=\"tt\">");
		print_safe(buffer);
		printf("&nbsp;&nbsp;</span><a href=\"");
		output_scriptptrget();
		printf("&amp;form=newevent&amp;draftmessage=");
		output_urlencoded(cgi("draftmessage"));
		printf("&amp;do.deleventtime=%s-%s\"><font size=\"-2\">"
		       "(%s)</font></a>",
		       libmail_str_time_t(t.start, buffer),
		       libmail_str_time_t(t.end, buffer+NUMBUFSIZE),
		       getarg("REMOVE")
		       );
	}
}

static int save_participant_names(const char *addr, const char *name,
				  void *vp)
{
	for (auto &p:my_participant_list)
	{
		if (p.address == addr && p.name.empty())
		{
			p.name=name;
		}
	}
	return (0);
}

void sqpcp_eventparticipants()
{
	if (my_participant_list.empty())
		return;

	(void)ab_get_nameaddr(save_participant_names, nullptr);

	printf("<table border=\"0\">");

	for (auto &m:my_participant_list)
	{
		printf("<tr><td><span class=\"tt\">");
		if (!m.address.empty())
			ab_nameaddr_show(m.name.c_str(), m.address.c_str());

		printf("</span></td><td>&nbsp;&nbsp;<a href=\"");

		output_scriptptrget();
		printf("&amp;form=newevent&amp;draftmessage=");
		output_urlencoded(cgi("draftmessage"));
		printf("&amp;do.delparticipant=");
		output_urlencoded(m.address.c_str());
		printf("\"><font size=\"-2\">(%s)</font></a></td></tr>\n",
		       getarg("REMOVE"));
	}
	printf("</table>");
}

void sqpcp_eventfrom()
{
	if (auth_getoptionenvint("wbnochangingfrom"))
	{
		printf("<span class=\"tt\">");
		print_safe(login_fromhdr());
		printf("</span>");
	}
	else
	{
		const char *p=cgi("headerfrom");

		if (!p || !*p)
			p=pref_from;
		if (!p || !*p)
			p=login_fromhdr();
		if (!p)
			p="";

		printf("<input type=\"text\" name=\"headerfrom\" size=\"60\" value=\"");
		output_attrencoded(p);
		printf("\" />");
	}
}

void sqpcp_eventtext()
{
	const char *p;

	printf("<textarea name=\"message\" cols=\"%d\" rows=\"15\" wrap=\"soft\">",
	       MYLINESIZE);
	p=cgi("draftmessage");

	if (p && *p)
	{
		rfc822::fdstreambuf fp=openoldfp(p, nullptr);

		if (!fp.error())
		{
			int dummy;

			newmsg_showfp(fp, &dummy);
		}
	}
	printf("</textarea>\n");
}

void sqpcp_eventattach()
{
	const char *p;

	p=cgi("draftmessage");

	if (p && *p)
	{
		attachments_head(NULL, cgi("pos"), p);
	}
}

/* ------- Display the cached event summary -------- */

static void print_event_subject(std::string_view, const char *, unsigned);
static void print_event_link_url(const char *, const char *);

void sqpcp_summary()
{
	FILE *fp;
	int cnt=0;
	time_t now;
	struct tm *tmptr;

	if (!sqpcp_mode())
		return;

	if (*cgi("do.calendarlogin"))
	{
		const char *userid=getenv("AUTHADDR");
		const char *password=cgi("password");

		if (userid && password)
		{
			do_pcplogin(userid, password, 1);
		}
	}

	if (sqpcp_has_groupware())
	{
		if (!sqpcp_loggedin())
		{
			insert_include("calendarlogin");
			return;
		}
	}

	time(&now);
	tmptr=localtime(&now);

	if (tmptr)
	{
		char *p=scriptptrget();

		std::string q;
		q.reserve(strlen(p)+200);
		q=p;
		q += "&amp;form=eventdaily&amp;clearcache=1&amp;date=";

		char yyyy[5];
		char mm[4];
		char dd[4];
		*std::to_chars(yyyy, yyyy+5, tmptr->tm_year + 1900).ptr=0;
		*std::to_chars(mm+1, mm+4, tmptr->tm_mon+1).ptr=0;
		*std::to_chars(dd+1, dd+4, tmptr->tm_mday).ptr=0;
		mm[0]='0';
		dd[0]='0';
		q += yyyy;
		q += mm+strlen(mm)-2;
		q += dd+strlen(dd)-2;

		printf(getarg("CALENDAR"), q.c_str());
	}

	printf("<table class=\"eventsummary\" width=\"100%%\">");

	if ((fp=fopen(CACHE, "r")) != NULL)
	{
		char buffer[BUFSIZ];
		int i;
		char last_date[256];
		char date[256];
		char date2[256];
		struct tm *tm;
		char yyyymmdd[100];

		char time1[128];
		char time2[128];

		last_date[0]=0;

		for (;;)
		{
			int c;
			struct cacherecord cr;

			i=0;

			while ((c=getc(fp)) != EOF && c != '\n')
			{
				if ((size_t)i < sizeof(buffer)-1)
					buffer[i++]=(char)c;
			}
			buffer[i]=0;
			if (i == 0 && c == EOF)
				break;

			parsecache_rec(buffer, &cr);

			if (pcp_fmttime(date, sizeof(date),
					cr.start, FMTTIME_DATE))
				continue;
			if (pcp_fmttime(date2, sizeof(date2),
					cr.end, FMTTIME_DATE))
				continue;

			if (pcp_fmttime(time1, sizeof(time1),
					cr.start,
					FMTTIME_TIME))
				continue;

			if (pcp_fmttime(time2,
					sizeof(time2),
					cr.end,
					FMTTIME_TIME))
				continue;

			tm=localtime(&cr.start);
			if (!tm)
				continue;
			sprintf(yyyymmdd, "%04d%02d%02d",
				tm->tm_year + 1900,
				tm->tm_mon + 1,
				tm->tm_mday);

			i=0;
			if (last_date[0] && strcmp(last_date, date) == 0)
			{
				if (strcmp(last_date, date2) == 0)
				{
					/* Same day as last event */

					printf("<tr><td align=\"left\" width=\"1%%\">&nbsp;</td>"
					       "<td align=\"left\">&nbsp;"
					       "<a href=\"");
					print_event_link_url(
						cr.eventid.c_str(),
						""
					);
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(time2);
					printf("</a></td>");
					i=1;
				}
				else
				{
					last_date[0]=0;
				}
			}
			else
			{
				last_date[0]=0;
				if (strcmp(date, date2) == 0)
					strcpy(last_date, date);
			}

			if (!i)
			{
				if (cnt)
					printf("<tr><td style=\"line-height: 0px; font-size: 8px\" colspan=\"3\">"
					       "&nbsp;"
					       "</td></tr>\n");
				if (strcmp(date, date2))
				{
					printf("<tr><td colspan=\"2\" style=\"white-space: nowrap\">"
					       "<a href=\"");
					print_event_link_url(
						cr.eventid.c_str(),
						""
					);
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(date);
					printf("&nbsp;");
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(date2);
					printf("&nbsp;");
					print_safe(time2);
					printf("</a></td>");
				}
				else
				{
					printf("<tr><td width=\"1%%\" style=\"white-space: nowrap\">"
					       "<a href=\"");
					print_event_link_url(
						cr.eventid.c_str(),
						""
					);
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(date);
					printf("</a></td><td style=\"white-space: nowrap\" >&nbsp;"
					       "<a href=\"");
					print_event_link_url(
						cr.eventid.c_str(),
						""
					);
					printf("&amp;date=%s\">", yyyymmdd);
					print_safe(time1);
					printf("&nbsp;-&nbsp;");
					print_safe(time2);
					printf("</a></td>");
				}
			}

			printf("<td width=\"100%%\">"
			       "<a href=\"");
			print_event_link_url(
				cr.eventid.c_str(),
				""
			);
			printf("&amp;date=%s\">&nbsp;&nbsp;", yyyymmdd);
			print_event_subject(cr.flags.c_str(), cr.subject.c_str(), 60);
			printf("</a></td></tr>\n");
			++cnt;
		}
		fclose(fp);
	}

	if (cnt == 0)
	{
		printf("<tr><td align=\"center\">%s</td></tr>\n",
		       getarg("NOEVENTS"));
	}

	printf("</table>\n");
	printf("<a href=\"");
	output_scriptptrget();
	printf("&amp;form=newevent\">%s</a>", getarg("NEWEVENT"));
}


static void print_event_subject(std::string_view flags, const char *subject,
				unsigned w)
{
	/* Print event flags first: CANCELLED... */

	for (auto b=flags.begin(), e=flags.end(); b != e; )
	{
		if (*b == ' ' || *b == '\t' || *b == '\r')
		{
			++b;
			continue;
		}

		auto p=b;

		b=std::find_if(b, e,
			       [](char c)
			       {
				       return c == ' ' || c == '\t' ||
					       c == '\n';
			       });

		std::string s{p, b};

		printf("%s", getarg(s.c_str()));
	}

	std::vector<std::string> address;

	rfc822::wrap_header(
		"subject",
		subject ? subject:"",
		w,
		sqwebmail_content_charset,
		std::back_inserter(address)
	);

	if (address.empty())
		address.push_back({});

	if (address.size() > 1)
		address[0] += "...";

	print_safe(address[0].c_str());
}

/* ------- New event support code -------- */

static int addtime(int);
static void addparticipant(int, const char *);

static rfc822::fdstreambuf openoldfp(const char *p, unsigned long *prev_size)
{
	struct stat stat_buf;

	CHECKFILENAME(p);

	std::string filename=maildir_find(INBOX "." DRAFTS, p);
	if (filename.empty())
		return {};

	rfc822::fdstreambuf oldfp{
		maildir_safeopen(filename.c_str(), O_RDONLY, 0)
	};

	if (oldfp.error())
		return oldfp;

	if (fstat(oldfp.fileno(), &stat_buf) < 0)
	{
		return {};
	}

	if (prev_size)
		*prev_size=stat_buf.st_size;
	return (oldfp);
}


/* ------------- Conflict indicators ---------------- */

struct conflict {
	std::string event_id;
	time_t start, end;
	std::string address;
	std::string subject;
} ;

static std::vector<struct conflict> conflict_list;

static void init_save_conflict()
{
	conflict_list.clear();
}

static int save_conflict(
	const char *event_id,
	time_t start,
	time_t end,
	const char *address,
	void *dummy)
{
	auto p=std::find_if(conflict_list.begin(), conflict_list.end(),
			    [end](const struct conflict &c)
			    {
				    return c.end > end;
			    });

	struct conflict c;

	c.event_id=event_id;
	c.start=start;
	c.end=end;
	c.address=address ? address : "";

	conflict_list.insert(p, std::move(c));

	return (0);
}

static int save_conflict_subj(struct PCP_retr *, const char *,
			      const char *, void *);

static void show_conflict_error(struct PCP *pcp)
{
	struct PCP_retr r;

	unsigned n=0;

	for (auto &p:conflict_list)
	{
		if (!p.event_id.empty() && (p.address.empty() ||
					    p.address == "@"))
			++n;
	}

	std::vector<std::string> l;
	l.reserve(n);

	for (auto &p:conflict_list)
		if (!p.event_id.empty() && (p.address.empty() ||
					    p.address == "@"))
		{
			l.push_back(p.event_id);
		}

	std::vector<char *> l_str;

	l_str.reserve(n+1);
	for (auto &s:l)
		l_str.push_back(s.data());
	l_str.push_back(nullptr);

	memset(&r, 0, sizeof(r));
	r.event_id_list=l_str.data();
	r.callback_headers_func=save_conflict_subj;

	if (n == 0 || pcp_retr(pcp, &r) == 0)
	{
		printf("<table border=\"0\" width=\"100%%\" class=\"small-error\">");
		for (auto &p:conflict_list)
		{
			char buffer[512];

			printf("<tr><td width=\"30\">&nbsp;</td><td><span class=\"tt\">");
			if (pcp_fmttimerange(buffer, sizeof(buffer),
					     p.start,
					     p.end) == 0)
			{
				print_safe(buffer);
			}
			printf("</span></td><td width=\"30\">&nbsp;</td><td width=\"100%%\"><span class=\"tt\">");
			if (!p.address.empty() && p.address != "@")
			{
				printf("%s", getarg("CONFLICTERR2"));
				print_safe(p.address.c_str());
			}
			else
				print_event_subject("", p.subject.c_str(), 60);
			printf("</span></td></tr>\n");
		}
		printf("<tr><td colspan=\"4\"><hr width=\"90%%\" /></td></tr></table>\n");
	}
	init_save_conflict();
}

static int save_conflict_subj(struct PCP_retr *r, const char *h,
			      const char *v, void *dummy)
{
	if (strcasecmp(h, "subject") || !v)
		return (0);

	std::string subject{v};

	for (auto &p:conflict_list)
		if (p.event_id == r->event_id)
		{
			p.subject=subject;
			break;
		}
	return (0);
}

/* -------------------------------------------------- */

static void showerror()
{
	const char *p;

	p=cgi("error");
	if (strcmp(p, "quota") == 0)
		printf("%s", getarg("QUOTAERR"));
	if (strcmp(p, "time") == 0)
		printf("%s", getarg("NOTIMEERR"));
	if (strcmp(p, "conflict") == 0)
	{
		struct PCP *pcp=sqpcp_calendar();

		printf("%s", getarg("CONFLICTERR"));
		if (pcp)
			show_conflict_error(pcp);
	}
	if (strcmp(p, "calendar") == 0)
	{
		printf(getarg("CALENDARERR"), cgi("pcperror"));
	}
	if (strcmp(p, "locked") == 0)
		printf("%s", getarg("LOCKERR"));
	if (strcmp(p, "notfound") == 0)
		printf("%s", getarg("NOTFOUNDERR"));
	if (strcmp(p, "eventlocked") == 0)
		printf("%s", getarg("EVENTLOCKEDERR"));
}

void sqpcp_newevent()
{
	int newdraftfd;
	const char *p;
	unsigned long prev_size=0;
	int errflag=0;
	bool do_newevent= *cgi("do.neweventtime") ? true:false;
	bool do_newparticipant= *cgi("do.addparticipant") ? true:false;
	bool do_delevent=false;
	bool do_delparticipant= *cgi("do.delparticipant") ? true:false;
	time_t delstart=0, delend=0;

	static std::string draftmessage_buf;

	showerror();

	if (!do_newevent)
	{
		if ((p=cgi("do.deleventtime")) && *p)
		{
			time_t a, b;

			if (parse_event_time(p, a, b))
			{
				delstart=a;
				delend=b;
				do_delevent=true;
			}
		}
	}

	p=cgi("draftmessage");

	if (!do_newevent && !do_delevent && !do_newparticipant
	    && !do_delparticipant)
	{
		if (*cgi("do.neweventpreview") && p && *p)
		{
			printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"1\" class=\"box-small-outer\"><tr><td>\n");
			printf("<table width=\"100%%\" border=\"0\" cellspacing=\"0\" cellpadding=\"4\" class=\"preview\"><tr><td>\n");
			newmsg_preview(p);
			printf("</td></tr></table>\n");
			printf("</td></tr></table>\n");
		}
		return;
	}

	rfc822::fdstreambuf oldfp;

	if (p && *p)
	{
		oldfp=openoldfp(p, &prev_size);
		if (oldfp.error())
			p="";
	}

	std::string draftfilename;

	if (p && *p)
	{
		newdraftfd=maildir_recreatemsg(INBOX "." DRAFTS, p, draftfilename);
	}
	else
	{
		newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, draftfilename);
		maildir_writemsgstr(newdraftfd, "X-Event: 1\n");

		draftmessage_buf=draftfilename;
		cgi_put("draftmessage", draftmessage_buf.c_str());
	}

	if (do_newevent || do_delevent || do_newparticipant
	    || do_delparticipant)
	{
		if (!oldfp.error())
		{
			rfc2045::entity::line_iter<false>::headers h{oldfp};

			do
			{
				const auto &[header, value] = h.name_content();

				time_t a, b;
				if (do_delevent && header == "x-event-time"
				    && parse_event_time(value, a, b))
				{
					if (a == delstart && b == delend)
					{
						continue;
					}
				}

				if (do_delparticipant
				    && header == "x-event-participant")
				{
					if (value ==
					    cgi("do.delparticipant"))
						continue;
				}

				if (header == "x-event-participant" &&
				    !value.empty())
					add_my_participant(value);

				if (header == "to")
					continue;
				/* To: header rebuilt later */

				maildir_writemsg(newdraftfd, header.data(),
						 header.size());
				maildir_writemsgstr(newdraftfd, ": ");
				maildir_writemsg(newdraftfd,
						 value.data(), value.size());
				maildir_writemsgstr(newdraftfd, "\n");
			} while (h.next());
		}

		if (do_newevent && addtime(newdraftfd))
		{
			errflag=1;
			printf("%s", getarg("TIMEERR"));
		}

		if (do_newparticipant)
		{
			const char *p=cgi("addressbookname");

			if (!*p)
				p=cgi("participant1");

			addparticipant(newdraftfd, p);
		}

		(void)ab_get_nameaddr(save_participant_names,
				      nullptr);
		sqpcp_eventend(); /* Deallocate participant list */

		maildir_writemsgstr(newdraftfd, "\n");
	}

	if (!oldfp.error())
	{
		char buf[BUFSIZ];

		while (1)
		{
			auto n=oldfp.sgetn(buf, sizeof(buf));

			if (n <= 0)
				break;

			maildir_writemsg(newdraftfd, buf, n);
		}
	}

	if (errflag)
	{
		maildir_deletenewmsg(newdraftfd, INBOX "." DRAFTS, draftfilename);
	}
	else if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 1,
				  prev_size))
	{
		printf("%s", getarg("QUOTAERR"));
	}
}

/* Split apart date/time string into space-separated words */

static std::vector<char *>mkargv(char *p)
{
	std::vector<char *>argv;

	while (*p)
	{
		if (isspace((int)(unsigned char)*p))
		{
			++p;
			continue;	/* Skip leading space */
		}

		argv.push_back(p);

		while (*p)  /* Look for next space */
		{
			if (isspace((int)(unsigned char)*p))
			{
				*p=0;
				++p;
				break;
			}
			++p;
		}
	}
	argv.push_back(nullptr);
	return (argv);
}

static int savetime(time_t, time_t, void *);

static int addtime(int newdraftfd)
{
	struct pcp_parse_datetime_info pdi;
	time_t starttime, endtime;
	int argn;
	int h, m;

	pdi.today_name=cgi("today");		/* Locale string */
	pdi.tomorrow_name=cgi("tomorrow");	/* Locale string */

	std::string t_buf=cgi("starttime");
	auto t_argv=mkargv(t_buf.data());

	argn=0;
	starttime=pcp_parse_datetime(&argn, t_argv.size()-1, t_argv.data(), &pdi);
	if (!starttime)
		return (-1);
	h=atoi(cgi("hours"));
	m=atoi(cgi("mins"));
	if (h < 0 || m < 0 || h > 999 || m > 59)
		return (-1);

	endtime=starttime + h * 60 * 60 + m * 60;

	t_buf=cgi("endtime");
	t_argv=mkargv(t_buf.data());

	if (t_argv.size() == 1)	/* Not a weekly event */
	{
		savetime(starttime, endtime, &newdraftfd);
	}
	else
	{
		argn=0;
		if (pcp_parse_datetime_until(starttime, endtime, &argn, t_argv.size()-1,
					     t_argv.data(),
					     atoi(cgi("recurring")),
					     savetime, &newdraftfd))
		{
			return (-1);
		}
	}
	return (0);
}

static int savetime(time_t from, time_t to, void *dummy)
{
	char buf[NUMBUFSIZE];
	int fd= *(int *)dummy;

	maildir_writemsgstr(fd, "X-Event-Time: ");
	maildir_writemsgstr(fd, libmail_str_time_t(from, buf));
	maildir_writemsgstr(fd, " ");
	maildir_writemsgstr(fd, libmail_str_time_t(to, buf));
	maildir_writemsgstr(fd, "\n");
	return (0);
}

static void addparticipant(int fd, const char *n)
{
	const char *domain;

	domain=getenv("AUTHADDR");

	if (domain)
		domain=strrchr(domain, '@');

	if (domain)
		++domain;
	else
		domain=myhostname();

	while (n && isspace((int)(unsigned char)*n))
		++n;

	if (!n || !*n)
		return;

	if (strchr(n, '\n') || strchr(n, '\r'))
		return;

	std::string nn;
	nn.reserve(strlen(n)+strlen(domain)+1);
	nn=n;

	while (nn.size() && isspace((int)(unsigned char)nn.back()))
		nn.pop_back();

	while (nn.size() && isspace((int)(unsigned char)nn.front()))
		nn.erase(0, 1);

	if (nn.find('@') == nn.npos)
	{
		nn += '@';
		nn += domain;
	}

	maildir_writemsgstr(fd, "X-Event-Participant: ");
	maildir_writemsgstr(fd, nn.c_str());
	maildir_writemsgstr(fd, "\n");
	add_my_participant(nn.c_str());
}

/* ------------- Save text ------------- */

static std::string savedraft()
{
	const char *p=cgi("draftmessage");
	std::string msg, filename;

	if (p && *p)
	{
		CHECKFILENAME(p);
	}

	filename=p && *p ? maildir_find(INBOX "." DRAFTS, p):"";

	msg=newmsg_createdraft_do(filename.c_str(), cgi("message"), NEWMSG_PCP);

	if (msg.empty())
		enomem();
	return (msg);
}

static void previewdraft(const char *msg, void (*func)(const char *))
{
	auto msg2=maildir_find(INBOX "." DRAFTS, msg);
	if (msg2.empty())
		enomem();

	size_t p=msg2.rfind('/');

	if (p == msg2.npos)
		p=0;
	else
		++p;

	auto msg2p=msg2.substr(p);

	cgi_put("draftmessage", msg2p.c_str());
	if (func)
		(*func)(msg2p.c_str());
	output_form("newevent.html");
}

void sqpcp_preview()
{
	auto msg=savedraft();
	previewdraft(msg.c_str(), NULL);
}

void sqpcp_postpone()
{
	savedraft();
	output_form("folders.html");
}

static void deleteattach(const char *);

void sqpcp_deleteattach()
{
	auto msg=savedraft();
	previewdraft(msg.c_str(), deleteattach);
}

static void deleteattach(const char *msg)
{
	attach_delete(msg);
}

static void doupload(const char *);

void sqpcp_uploadattach()
{
	auto msg=savedraft();
	previewdraft(msg.c_str(), doupload);
}

static void doupload(const char *msg)
{
	int flag;

	flag=attach_upload(msg, NULL, NULL);

	if (flag)
		cgi_put("error", "quota");
}

static void doattpubkey(const char *);

void sqpcp_attachpubkey()
{
	auto msg=savedraft();
	previewdraft(msg.c_str(), doattpubkey);
}

static void doattpubkey(const char *msg)
{
	int flag;

	flag=attach_upload(msg, cgi("pubkey"), NULL);

	if (flag)
		cgi_put("error", "quota");
}

static void doattprivkey(const char *msg);

void sqpcp_attachprivkey()
{
	auto msg=savedraft();
	previewdraft(msg.c_str(), doattprivkey);
}

static void doattprivkey(const char *msg)
{
	int flag;

	flag=attach_upload(msg, NULL, cgi("privkey"));

	if (flag)
		cgi_put("error", "quota");
}

/* ---------------- Save event ------------------ */

struct saveinfo {
	std::vector<PCP_event_time> times;
	std::vector<std::string> participants;
	std::string old_eventid;
} ;

static int init_saveinfo(struct saveinfo *si, rfc822::fdstreambuf &fp)
{
	rfc2045::entity::line_iter<false>::headers h{fp};

	do
	{
		time_t a, b;

		const auto &[header, value] = h.name_content();
		if (header == "x-event-participant" && !value.empty())
		{
			si->participants.push_back(
				{
					value.begin(), value.end()
				}
			);
		}
		else if (header == "x-event-time" &&
			 parse_event_time(value, a, b))
		{
			si->times.push_back({a, b});
		}

		if (header == "x-old-eventid" && !value.empty())
		{
			si->old_eventid=std::string{value.begin(),
				value.end()
			};
		}
	} while (h.next());
	return (0);
}

static void dropquota(const char *filename, int fd)
{
	unsigned long filesize=0;

	if (maildir_parsequota(filename, &filesize))
	{
		struct stat stat_buf;

		if (fstat(fd, &stat_buf))
			stat_buf.st_size=0;
		filesize=stat_buf.st_size;
	}

	maildir_quota_deleted(".", (int64_t)-filesize, -1);
}

/* ------------------------------------------------------------------------
**
** Save a calendar event
** ----------------------------------------------------------------------*/

static int dosave(rfc822::fdstreambuf &, struct saveinfo &);

void sqpcp_save()
{
	std::string sentmsg;
	struct saveinfo si;
	int isgpgerr;

	auto msg=savedraft();
	if (*cgi("error"))	/* Error, go back to the screen */
	{
		previewdraft(msg.c_str(), NULL);
		return;
	}

	auto fp=openoldfp(msg.c_str(), NULL);
	if (fp.error())
	{
		enomem();
	}

	if (init_saveinfo(&si, fp))
	{
		enomem();
	}

	if (si.times.empty())
	{
		cgi_put("error", "time");
		previewdraft(msg.c_str(), NULL);
		return;
	}

	sentmsg=newmsg_createsentmsg(msg.c_str(), &isgpgerr);

	/* Immediately remove the formatted event text from the sent folder */

	if (!sentmsg.empty())
	{
		sentmsg=maildir_find(INBOX "." SENT, sentmsg.c_str());
	}

	if (sentmsg.empty())
	{
		cgi_put("error", "quota");	/* TODO: gpgerr */
		previewdraft(msg.c_str(), NULL);
		return;
	}


	fp=rfc822::fdstreambuf{open(sentmsg.c_str(), O_RDONLY)};

	if (fp.error())
	{
		enomem();
		return;
	}

	fcntl(fp.fileno(), F_SETFD, FD_CLOEXEC);

	unlink(sentmsg.c_str());
	dropquota(sentmsg.c_str(), fp.fileno());

	if (dosave(fp, si))
	{
		previewdraft(msg.c_str(), NULL);
		return;
	}

	auto p=maildir_find(INBOX "." DRAFTS, msg.c_str());

	fp=rfc822::fdstreambuf{
		!p.empty() ? open(p.c_str(), O_RDONLY):-1
	};

	if (!p.empty())
		unlink(p.c_str());

	if (!fp.error())
	{
		dropquota(p.c_str(), fp.fileno());
	}
	output_form("folders.html");
}

/* With all the preliminaries out of the way, put it on the calendar */

static void saveerror(struct PCP *pcp, const int *xerror)
{
	static char *errmsgbuf=0;
	const char *p;

	if (xerror)
		switch (*xerror) {
		case PCP_ERR_SYSERR:	/* Can be deliberate, see addacl() */
			cgi_put("error", "calendar");
			cgi_put("pcperror", strerror(errno));
			return;
		case PCP_ERR_LOCK:
			cgi_put("error", "locked");
			return;
		case PCP_ERR_CONFLICT:
			cgi_put("error", "conflict");
			return;
		case PCP_ERR_EVENTNOTFOUND:
			cgi_put("error", "notfound");
			return;
		case PCP_ERR_EVENTLOCKED:
			cgi_put("error", "eventlocked");
			return;
		}

	cgi_put("error", "calendar");

	if (errmsgbuf)
		free(errmsgbuf);

	/*
	** Save err msg into a static buffer, because err msg text memory
	** may go away after the handle is closed.
	*/

	p=pcp_errmsg(pcp);
	errmsgbuf=strdup(p ? p:"");
	cgi_put("pcperror", errmsgbuf);
}

struct proxy_update_list {
	std::unordered_set<std::string> new_list, delete_list;
} ;

static void proxy_update_list_save(const char *action,
				   const char *userid,
				   void *voidarg)
{
	struct proxy_update_list *pul=(struct proxy_update_list *)voidarg;

	if (strcmp(action, "NEW") == 0)
		pul->new_list.insert(userid);
	else if (strcmp(action, "DELETE") == 0)
		pul->delete_list.insert(userid);
}
static void proxy_notify_email_msg(rfc822::fdstreambuf &,
				   const std::unordered_set<std::string> &,
				   const char *,
				   const struct PCP_event_time *,
				   unsigned);

static void proxy_notify_email(rfc822::fdstreambuf &f,
			       struct proxy_update_list *pul,
			       const struct PCP_event_time *t,
			       unsigned tn)
{
	proxy_notify_email_msg(f, pul->new_list, "eventnotifynew.txt",
			       t, tn);
	proxy_notify_email_msg(f, pul->delete_list, "eventnotifydelete.txt",
			       NULL, 0);
}

static void dosendnotice(FILE *, FILE *, rfc822::fdstreambuf &,
			 const std::unordered_set<std::string> &,
			 const char *, const struct PCP_event_time *,
			 unsigned);

static void proxy_notify_email_msg(rfc822::fdstreambuf &f,
				   const std::unordered_set<std::string> &l,
				   const char *templatestr,
				   const struct PCP_event_time *t,
				   unsigned tn)
{
	FILE *tmpfp;
	pid_t p, p2;
	int waitstat;
	int pipefd[2];
	FILE *tofp;
	const char *returnaddr=login_returnaddr();
	char subjectlabel[100];

	if (l.empty())
		return;

	f.pubseekpos(0);

	if (f.error())
	{
		fprintf(stderr, "CRIT: seek failed: %s\n", strerror(errno));
		return;
	}

	subjectlabel[0]=0;

	if ((tmpfp=open_langform(sqwebmail_content_language,
				 "eventnotifysubject.txt", 0)) != NULL)
	{
		if (fgets(subjectlabel, sizeof(subjectlabel), tmpfp) == NULL)
			subjectlabel[0]=0;
		else
		{
			char *p=strchr(subjectlabel, '\n');

			if (p) *p=0;
		}
		fclose(tmpfp);
	}
	if (subjectlabel[0] == 0)
		strcpy(subjectlabel, "[calendar]");

	if ((tmpfp=open_langform(sqwebmail_content_language, templatestr, 0))
	    == NULL)
	{
		fprintf(stderr, "CRIT: %s: %s\n", templatestr, strerror(errno));
		return;
	}

	signal(SIGPIPE, SIG_IGN);

	if (pipe(pipefd) < 0)
	{
		fclose(tmpfp);
		fprintf(stderr, "CRIT: pipe: %s\n", strerror(errno));
		return;
	}

	p=fork();

	if (p < 0)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		fclose(tmpfp);
		fprintf(stderr, "CRIT: fork: %s\n", strerror(errno));
		return;
	}

	if (p == 0)
	{
		dup2(pipefd[0], 0);
		close(pipefd[0]);
		close(pipefd[1]);
		execl(SENDITSH, "sendit.sh", returnaddr,
                                sqwebmail_mailboxid, NULL);
		fprintf(stderr, "CRIT: exec " SENDITSH ": %s\n", strerror(errno));
		exit(1);
	}
	close(pipefd[0]);
	if ((tofp=fdopen(pipefd[1], "w")) == NULL)
	{
		fprintf(stderr, "CRIT: exec " SENDITSH ": %s\n", strerror(errno));
	}
	else
	{
		dosendnotice(tofp, tmpfp, f, l, subjectlabel, t, tn);
	}
	fclose(tofp);
	close(pipefd[1]);
	fclose(tmpfp);

	waitstat=256;
	while ((p2=wait(&waitstat)) != p && p2 >= 0)
		;

	if (!WIFEXITED(waitstat) || WEXITSTATUS(waitstat))
		fprintf(stderr, "CRIT: event notify mail failed\n");
}

/*
** Ok, the preliminaries are out of the way, now spit it out.
*/

static void dosendnotice(FILE *tofp,	/* Pipe to sendit.sh */
			 FILE *tmpfp,	/*
					** Template file with MIME headers and
					** canned verbiage
					*/
			 rfc822::fdstreambuf &eventfp,	/* Original event */
			 const std::unordered_set<std::string> &idlist,
			 const char *subjectlabel,
			 const struct PCP_event_time *time_list,
			 unsigned n_time_list)
{
	int c;
	unsigned u;

	rfc2045::entity::line_iter<false>::headers h{eventfp};
	do
	{
		const auto &[header, value] = h.name_content();

		if (header == "from" || header == "date")
		{
			fwrite(header.data(), header.size(), 1, tofp);
			fprintf(tofp, ": ");
			fwrite(value.data(), value.size(), 1, tofp);
			fprintf(tofp, "\n");
		}
		else if (header == "subject")
		{
			fprintf(tofp, "Subject: %s ", subjectlabel);
			fwrite(value.data(), value.size(), 1, tofp);
			fprintf(tofp, "\n");
		}
	} while (h.next());

	for (const auto &id: idlist)
	{
		fprintf(tofp, "To: %s\n", id.c_str());
	}

	while ((c=getc(tmpfp)) != EOF)
		putc(c, tofp);

	for (u=0; u<n_time_list; u++)
	{
		char buffer[200];

		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     time_list[u].start,
				     time_list[u].end) == 0)
		{
			fprintf(tofp, "     %s\n", buffer);
		}
	}
}

static int dosave(rfc822::fdstreambuf &fp, struct saveinfo &si)
{
	struct PCP *pcp=sqpcp_calendar();
	struct PCP_save_event se;
	struct PCP_new_eventid *nei;
	struct PCP_commit c;

	struct proxy_update_list pul;

	if (!pcp)
	{
		cgi_put("error", "calendar");	/* TODO: actual error */
		cgi_put("pcperror", "");
		return (-1);
	}

	memset(&se, 0, sizeof(se));
	se.write_event_fd=fp.fileno();

	std::vector<PCP_event_participant> participants;

	participants.reserve(si.participants.size());
	for (auto &sp:si.participants)
	{
		PCP_event_participant p;

		p.address=sp.c_str();
		participants.push_back(p);
	}
	se.event_participants=participants.data();;
	se.n_event_participants=participants.size();

	if (*cgi("okconflict"))
		se.flags |= PCP_OK_CONFLICT;
	if (*cgi("okerrors"))
		se.flags |= PCP_OK_PROXY_ERRORS;

	nei=pcp_new_eventid(pcp, si.old_eventid.c_str(), &se);
	if (!nei)
	{
		saveerror(pcp, NULL);
		return (-1);
	}

	memset(&c, 0, sizeof(c));
	c.event_times=si.times.data();
	c.n_event_times=si.times.size();
	c.flags=se.flags;

	init_save_conflict();
	c.add_conflict_callback=save_conflict;

	c.proxy_callback= &proxy_update_list_save;
	c.proxy_callback_ptr= &pul;

	if (pcp_commit(pcp, nei, &c))
	{
		saveerror(pcp, &c.errcode);
		pcp_destroy_eventid(pcp, nei);
		return (-1);
	}
	pcp_destroy_eventid(pcp, nei);
	unlink(CACHE);	/* Have it rebuilt */
	proxy_notify_email(fp, &pul, c.event_times, c.n_event_times);
	refreshcache(pcp);
	return (0);
}

/* -------------- Daily stuff --------------- */

void sqpcp_todays_date()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	char buf[100];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end)
	    || pcp_fmttime(buf, sizeof(buf), start, FMTTIME_DATE))
		return;

	print_safe(buf);
}

void sqpcp_todays_date_verbose()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	char buf[500];
	struct tm *tmptr;

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end)
	    || (tmptr=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);
}

void sqpcp_weeklylink()
{
	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%s", cgi("date"));
}

void sqpcp_monthlylink()
{
	const char *p=cgi("date");

	if (!*p)
		p=cgi("weekof");
	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%s", p);
}

#define VIEW_DAILY	0
#define	VIEW_WEEKLY	1
#define VIEW_MONTHLY	2

static void do_daily_view(std::vector<cacherecord> &, int,
			  time_t *, time_t *);

static void show_pcp_errmsg(const char *p)
{
	printf("<pre class=\"error\">");
	output_attrencoded_oknl_fp(p, stdout);
	printf("</pre>");
}

void sqpcp_daily_view()
{
	unsigned y, m, d;
	time_t start;
	time_t end;

	struct PCP *pcp;
	std::vector<cacherecord> recs;

	if (*cgi("clearcache"))
		unlink(CACHE);

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	if ((pcp=sqpcp_calendar()) == NULL)
	{
		printf("<span class=\"error\">%s</span>", strerror(errno));
		return;
	}

	if (!createcache(pcp, recs, start, end))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	(void)do_daily_view(recs, VIEW_DAILY, NULL, NULL);
}

static void print_event_link_url(const char *id, const char *extra)
{
	output_scriptptrget();
	printf("%s&amp;form=eventshow&amp;eventid=", extra);
	output_urlencoded(id);
	if (*cgi("date"))
		printf("&amp;date=%s", cgi("date"));
	if (*cgi("weekof"))
		printf("&amp;weekof=%s", cgi("weekof"));
	if (*cgi("monthof"))
		printf("&amp;monthof=%s", cgi("monthof"));
}

static void print_event_link(const char *id, const char *extra,
			     const char *extra2)
{
	printf("<a href=\"");
	print_event_link_url(id, extra);
	printf("\" %s >", extra2);
}

static void do_daily_view(std::vector<cacherecord> &recs, int viewtype,
			  time_t *start_ptr, time_t *end_ptr)
{
	bool printed=false;

	printf("<table width=\"100%%\">");

	for (auto &rec:recs)
	{
		char date1[256];
		char date2[256];

		char time1[128];
		char time2[128];

		time_t start=rec.start;
		time_t end=rec.end;

		if (start_ptr && *start_ptr >= end)
			continue;

		if (end_ptr && *end_ptr <= start)
			continue;

		if ( start_ptr && *start_ptr > start)
			start= *start_ptr;

		if ( end_ptr && *end_ptr < end)
			end= *end_ptr;

		if (pcp_fmttime(date1, sizeof(date1),
				start, FMTTIME_DATE))
			continue;
		if (pcp_fmttime(date2, sizeof(date2),
				end, FMTTIME_DATE))
				continue;

		printed=true;

		if (strcmp(date1, date2) && viewtype == VIEW_DAILY)
		{
			char timerange[512];

			if (pcp_fmttimerange(timerange, sizeof(timerange),
					     start, end))
				continue;

			printf("<tr><td align=\"left\" style=\"white-space: nowrap\">");
			print_event_link(rec.eventid.c_str(),
					 "", "class=\"dailyeventtimes\"");
			print_safe(timerange);
		}
		else
		{
			if (pcp_fmttime(time1, sizeof(time1),
					start,
					FMTTIME_TIME))
				continue;

			if (pcp_fmttime(time2,
					sizeof(time2),
					end,
					FMTTIME_TIME))
				continue;

			printf("<tr><td align=\"left\" style=\"white-space: nowrap\">");
			print_event_link(rec.eventid.c_str(),
						"", "class=\"dailyeventtimes\"");
			print_safe(time1);
			printf("&nbsp;-&nbsp;");
			print_safe(time2);
		}
		printf("</a>");

		if (viewtype == VIEW_DAILY)
		{
			printf("</td><td width=\"100%%\">");
		}
		else
			printf("<br />");

		printf("&nbsp;&nbsp;");
		print_event_link(rec.eventid.c_str(),
				"", "class=\"dailyeventsubject\"");
		print_event_subject(rec.flags, rec.subject.c_str(),
				    viewtype == VIEW_DAILY ? 80:15);
		printf("</a>");
		if (viewtype != VIEW_DAILY)
			printf("<br />&nbsp;");

		printf("</td></tr>\n");
	}

	if (!printed)
	{
		printf("<tr><td align=\"center\">%s</td></tr>",
		       getarg("NOEVENTS"));
	}

	printf("</table>\n");
}

void sqpcp_prevday()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tm;
	char buf[256];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	start -= 12 * 60 * 60;

	if ((tm=localtime(&start)) == NULL)
		return;

	y=tm->tm_year + 1900;
	m=tm->tm_mon + 1;
	d=tm->tm_mday;

	if (pcp_parse_ymd(y, m, d, &start, &end)
	    || (tm=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("PREVDAY"), tm) == 0)
		return;

	printf("<a class=\"dailynextprev\" href=\"");
	output_scriptptrget();
	printf("&amp;form=eventdaily&amp;date=%04d%02d%02d\">%s</a>", y, m, d, buf);
}

void sqpcp_nextday()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tm;
	char buf[256];

	if (sscanf(cgi("date"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
		return;

	start=end;

	if ((tm=localtime(&start)) == NULL)
		return;

	y=tm->tm_year + 1900;
	m=tm->tm_mon + 1;
	d=tm->tm_mday;

	if (pcp_parse_ymd(y, m, d, &start, &end)
	    || (tm=localtime(&start)) == NULL
	    || strftime(buf, sizeof(buf), getarg("NEXTDAY"), tm) == 0)
		return;

	printf("<a class=\"dailynextprev\" href=\"");
	output_scriptptrget();
	printf("&amp;form=eventdaily&amp;date=%04d%02d%02d\">%s</a>", y, m, d, buf);
}

/* -------------- Display event stuff --------------- */

struct display_retr {
	FILE *f;

	// time_list is a tuple of start and end time.
	std::vector<std::tuple<time_t, time_t>> time_list;
	std::vector<std::string> participant_list;
} ;

static int save_displayed_event(struct PCP_retr *, const char *, int, void *);

static int set_status(struct PCP_retr *, int, void *);

void sqpcp_displayeventinit()
{
	struct PCP *pcp;
	const char *event_id_list[2];
	struct PCP_retr r;

	showerror();

	pcp=sqpcp_calendar();

	if (!pcp)
		return;

	event_id_list[0]=cgi("eventid");
	event_id_list[1]=0;

	init_save_conflict();
	if (*cgi("docancel"))
	{
		if (pcp_cancel(pcp, event_id_list[0], NULL))
			show_pcp_errmsg(pcp_errmsg(pcp));
		unlink(CACHE);
	}

	if (*cgi("douncancel"))
	{
		struct PCP_uncancel u;
		int flags=0;

		memset(&u, 0, sizeof(u));

		if (*cgi("okconflict"))
			flags |= PCP_OK_CONFLICT;

		u.uncancel_conflict_callback=save_conflict;
		if (pcp_uncancel(pcp, event_id_list[0], flags, &u))
		{
			saveerror(pcp, &u.errcode);
			showerror();
			if (u.errcode == PCP_ERR_CONFLICT)
				cgi_put("okconflict", "1");
		}
		unlink(CACHE);
	}

	memset(&r, 0, sizeof(r));
	r.event_id_list=event_id_list;
	r.callback_retr_status=set_status;

	if (pcp_retr(pcp, &r))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

}

static int set_status(struct PCP_retr *pcp, int status, void *dummy)
{
	cgi_put("event_cancelled", status & LIST_CANCELLED ? "1":"");
	cgi_put("event_booked", status & LIST_BOOKED ? "1":"");
	cgi_put("event_proxy", status & LIST_PROXY ? "1":"");
	return (0);
}

static int save_displayed_date(struct PCP_retr *r, time_t start, time_t end,
			       void *vp)
{
	display_retr *dr=reinterpret_cast<display_retr *>(vp);

	auto ptr=dr->time_list.begin();
	while (ptr != dr->time_list.end() && std::get<0>(*ptr) < start)
		++ptr;

	dr->time_list.insert(ptr, {start, end});
	return (0);
}

static int save_displayed_participants(struct PCP_retr *r, const char *address,
				       const char *dummy, void *vp)
{
	display_retr *dr=reinterpret_cast<display_retr *>(vp);

	auto ptr=dr->participant_list.begin();
	while (ptr != dr->participant_list.end() &&
	       strcasecmp(ptr->c_str(), address) < 0)
		++ptr;

	dr->participant_list.insert(ptr, address);
	return (0);
}

void sqpcp_displayevent()
{
	struct PCP *pcp=sqpcp_calendar();
	const char *event_id_list[2];
	struct PCP_retr r;
	display_retr dr;
	maildir::tmpcreate_info createInfo;

	if (!pcp)
		return;

	event_id_list[0]=cgi("eventid");
	event_id_list[1]=0;

	memset(&r, 0, sizeof(r));
	r.event_id_list=event_id_list;
	r.callback_arg=&dr;
	r.callback_rfc822_func=save_displayed_event;
	r.callback_retr_date=save_displayed_date;
	r.callback_retr_participants=save_displayed_participants;

	maildir_purgemimegpg(); /* Delete previous :calendar: file */

	createInfo.uniq=":calendar:";
	createInfo.doordie=true;

	if ((dr.f=createInfo.fp()) == NULL)
	{
		error(strerror(errno));
	}

	cgi_put(MIMEGPGFILENAME,
		createInfo.tmpname.c_str()+createInfo.tmpname.rfind('/')+1);

	if (pcp_retr(pcp, &r))
	{
		fclose(dr.f);
		cgi_put(MIMEGPGFILENAME, "");
		unlink(createInfo.tmpname.c_str());
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}
	fclose(dr.f);

	printf("<table class=\"calendarevent\" align=\"center\" border=\"0\"><tr valign=\"top\"><th align=\"left\">%s</th><td>",
	       getarg("EVENT"));

	for (auto &t : dr.time_list)
	{
		char buffer[512];

		if (pcp_fmttimerange(buffer, sizeof(buffer),
				     std::get<0>(t), std::get<1>(t)))
			continue;

		printf("<span class=\"tt\">");
		print_safe(buffer);
		printf("</span><br />\n");
	}
	printf("</td></tr>\n");

	if (!dr.participant_list.empty())
	{
		printf("<tr valign=\"top\"><th align=\"left\">%s</th><td>",
		       getarg("PARTICIPANTS"));
		for (auto &p:dr.participant_list)
		{
			printf("<span class=\"tt\">&lt;");
			print_safe(p.c_str());
			printf("&gt;</span><br />\n");
		}
		printf("</td></tr>");
	}
	printf("</table>\n");
	folder_showmsg(INBOX "." DRAFTS, 0);
	cgi_put(MIMEGPGFILENAME, "");
}

static int save_displayed_event(struct PCP_retr *r,
				const char *buf, int cnt,
				void *vp)
{
	if (fwrite( buf, cnt, 1,
		reinterpret_cast<display_retr *>(vp)->f) != 1)
		return (-1);

	return (0);
}


static void back_to_summary()
{
	const char *p;

	if (*(p=cgi("monthof")) != 0)
	{
		printf("&amp;form=eventmonthly&amp;monthof=%s", p);
	}
	else if (*(p=cgi("weekof")) != 0)
	{
		printf("&amp;form=eventweekly&amp;weekof=%s", p);
	}
	else
	{
		p=cgi("date");
		printf("&amp;form=eventdaily&amp;date=%s", p);
	}
}

void sqpcp_eventbacklink()
{
	output_scriptptrget();
	back_to_summary();
}

void sqpcp_eventeditlink()
{
	const char *p=cgi("date");

	output_scriptptrget();
	printf("&amp;form=event-edit&amp;date=%s&amp;eventid=", p);
	output_urlencoded(cgi("eventid"));
}

void sqpcp_eventdeletelink()
{
	const char *p=cgi("date");

	output_scriptptrget();
	printf("&amp;eventid=");

	output_urlencoded(cgi("eventid"));
	printf("&amp;form=eventdelete&amp;date=%s", p);
}

void sqpcp_eventcanceluncancellink()
{
	print_event_link_url(cgi("eventid"), *cgi("event_cancelled")
			 ? (*cgi("okconflict") ?
			    "&amp;okconflict=1&amp;douncancel=1"
			    :"&amp;douncancel=1")
			     :"&amp;docancel=1");
}

void sqpcp_eventcanceluncancelimage()
{
	printf("%s", getarg(*cgi("event_cancelled")
			    ? "UNCANCELIMAGE":"CANCELIMAGE"));
}

void sqpcp_eventcanceluncanceltext()
{
	printf("%s", getarg(*cgi("event_cancelled")
			    ? "UNCANCELTEXT":"CANCELTEXT"));
}

void sqpcp_deleteeventinit()
{
	sqpcp_displayeventinit();
	if (*cgi("event_proxy"))
		printf("%s", getarg("PROXYWARN"));
}

static int save_orig_headers(struct PCP_retr *pcp,
			     const char *h, const char *v, void *vp)
{
	auto fp=reinterpret_cast<rfc822::fdstreambuf *>(vp);

	if (strcasecmp(h, "Date"))
	{
		std::ostream o{fp};
		o << h << ": " << v << "\n";
	}
	return (0);
}


void sqpcp_dodelete()
{
	struct PCP *pcp=sqpcp_calendar();
	struct PCP_retr r;
	const char *event_list_ary[2];
	struct PCP_delete del;
	struct proxy_update_list pul;

	if (!pcp)
		return;

	memset(&del, 0, sizeof(del));
	del.id=cgi("eventid");

	memset(&r, 0, sizeof(r));
	event_list_ary[0]=del.id;
	event_list_ary[1]=NULL;
	r.event_id_list=event_list_ary;
	r.callback_headers_func=save_orig_headers;

	auto tmpfp=rfc822::fdstreambuf::tmpfile();
	r.callback_arg=&tmpfp;
	pcp_retr(pcp, &r);

	{
		time_t t;

		time(&t);

		std::ostream o{&tmpfp};
		o << "Date: " << rfc822_mkdate(t) << "\n\n";
	}

	del.proxy_callback=&proxy_update_list_save;
	del.proxy_callback_ptr=&pul;

	if (pcp_delete(pcp, &del))
	{
		saveerror(pcp, &del.errcode);
		output_form("eventdelete.html");
	}
	else
	{
		proxy_notify_email(tmpfp, &pul, NULL, 0);
	}
	unlink(CACHE);
	output_form("eventdaily.html");
}

/* ------------- Bring in an event to edit -------------------------- */

static int doeventedit(struct PCP *, int);

int sqpcp_eventedit()
{
	struct PCP *pcp=sqpcp_calendar();
	int newdraftfd;
	std::string draftfilename;

	if (!pcp)
		return (-1);

	newdraftfd=maildir_createmsg(INBOX "." DRAFTS, 0, draftfilename);
	if (doeventedit(pcp, newdraftfd))
	{
		maildir_deletenewmsg(newdraftfd, INBOX "." DRAFTS, draftfilename);
	}
	else if (maildir_closemsg(newdraftfd, INBOX "." DRAFTS, draftfilename, 1, 0))
	{
		cgi_put("error", "quota");
	}
	else
	{
		static std::string filenamebuf;

		filenamebuf=draftfilename;
		cgi_put("draftmessage", filenamebuf.c_str());
		return (0);
	}
	return (-1);
}

struct getedit_info {
	int fd;
	int flag;

	int in_headers;
	int sol;
	int skiph;

} ;

static int get_date(struct PCP_retr *r, time_t start, time_t end, void *vp)
{
	char buf[NUMBUFSIZE];
	struct getedit_info *ge=(struct getedit_info *)vp;

	maildir_writemsgstr(ge->fd, "X-Event-Time: ");
	maildir_writemsgstr(ge->fd, libmail_str_time_t(start, buf));
	maildir_writemsgstr(ge->fd, " ");
	maildir_writemsgstr(ge->fd, libmail_str_time_t(end, buf));
	maildir_writemsgstr(ge->fd, "\n");

	return (0);
}

static int get_participants(struct PCP_retr *r, const char *address,
			    const char *dummy, void *vp)
{
	struct getedit_info *ge=(struct getedit_info *)vp;

	maildir_writemsgstr(ge->fd, "X-Event-Participant: ");
	maildir_writemsgstr(ge->fd, address);
	maildir_writemsgstr(ge->fd, "\n");

	return (0);
}

static int get_msgtext(struct PCP_retr *r, const char *ptr, int n, void *vp)
{
	struct getedit_info *ge=(struct getedit_info *)vp;

	/* We want to drop all X headers when we read in this event */
	/* Also, drop CRs */

	ge->flag=1;

	while (n)
	{
		int i;

		if (!ge->in_headers)	/* Write out msg body */
		{
			while (n)
			{
				if (*ptr == '\r')
				{
					++ptr;
					--n;
					continue;
				}

				for (i=0; i<n; i++)
					if (ptr[i] == '\r')
						break;

				maildir_writemsg(ge->fd, ptr, i);
				ptr += i;
				n -= i;
			}
			break;
		}

		if (*ptr == '\r')
		{
			++ptr;
			--n;
			continue;
		}

		if (*ptr == '\n')
		{
			if (!ge->skiph)
				maildir_writemsgstr(ge->fd, "\n");

			ge->skiph=0;
			if (ge->sol)
				ge->in_headers=0;	/* End of headers */
			ge->sol=1;
			++ptr;
			--n;
			continue;
		}

		if (ge->sol && (*ptr == 'x' || *ptr == 'X'))
			ge->skiph=1;

		for (i=0; i<n; i++)
			if (ptr[i] == '\r' || ptr[i] == '\n')
				break;

		if (!ge->skiph)	/* Skip X- header */
			maildir_writemsg(ge->fd, ptr, i);
		ptr += i;
		n -= i;
	}
	return (0);
}

static int doeventedit(struct PCP *pcp, int fd)
{
	const char *p=cgi("eventid");
	struct PCP_retr r;
	struct getedit_info ge;

	const char *eventid[2];

	if (!p || !*p)
		return (-1);

	maildir_writemsgstr(fd, "X-Event: 1\nX-Old-EventId: ");
	maildir_writemsgstr(fd, p);
	maildir_writemsgstr(fd, "\n");

	memset(&r, 0, sizeof(r));
	eventid[0]=p;
	eventid[1]=NULL;

	r.event_id_list=eventid;

	ge.fd=fd;
	ge.flag=0;
	r.callback_arg=&ge;

	r.callback_retr_date=get_date;
	r.callback_retr_participants=get_participants;

	if (pcp_retr(pcp, &r))
	{
		saveerror(pcp, &r.errcode);
		return (-1);
	}

	memset(&r, 0, sizeof(r));
	eventid[0]=p;
	eventid[1]=NULL;

	r.event_id_list=eventid;

	ge.fd=fd;
	ge.flag=0;
	r.callback_arg=&ge;

	ge.in_headers=1;
	ge.sol=1;
	ge.skiph=0;

	r.callback_rfc822_func=get_msgtext;
	if (pcp_retr(pcp, &r))
	{
		saveerror(pcp, &r.errcode);
		return (-1);
	}

	if (!ge.flag)
	{
		cgi_put("error", "notfound");
		return (-1);
	}

	return (0);
}

/* ------ Weekly stuff ------- */

static void prevday(time_t *tm)
{
	struct tm *tmptr;
	time_t t= *tm - 12 * 60 * 60;

	if ((tmptr=localtime(&t)) == NULL)
	{
		enomem();
		return;
	}

	if (pcp_parse_ymd(tmptr->tm_year + 1900, tmptr->tm_mon + 1,
			  tmptr->tm_mday, tm, &t))
	{
		enomem();
	}
}

static void nextday(time_t *tm)
{
	struct tm *tmptr;
	time_t t= *tm + 36 * 60 * 60;

	if ((tmptr=localtime(&t)) == NULL)
	{
		enomem();
		return;
	}

	if (pcp_parse_ymd(tmptr->tm_year + 1900, tmptr->tm_mon + 1,
			  tmptr->tm_mday, tm, &t))
	{
		enomem();
	}
}

static time_t get_start_of_week()
{
	unsigned y, m, d;
	time_t start;
	time_t end;
	struct tm *tmptr;
	int i;

	if (sscanf(cgi("weekof"), "%4u%2u%2u", &y, &m, &d) != 3
	    || pcp_parse_ymd(y, m, d, &start, &end))
	{
		time(&start);
		if ((tmptr=localtime(&start)) == NULL)
		{
			enomem();
			return (0);
		}
		y=tmptr->tm_year + 1900;
		m=tmptr->tm_mon + 1;
		d=tmptr->tm_mday;

		if (pcp_parse_ymd(y, m, d, &start, &end))
		{
			enomem();
			return (0);
		}
	}

	for (i=0; i<7; i++)
	{
		tmptr=localtime(&start);
		if (!tmptr)
			enomem();

		if (tmptr->tm_wday == pref_startofweek)
			break;

		prevday(&start);
	}

	return (start);
}

void sqpcp_show_cal_week()
{
	time_t start=get_start_of_week();
	struct tm *tmptr;
	char buf[512];

	if ((tmptr=localtime(&start)) == NULL)
		return;

	if (strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);
}

void sqpcp_show_cal_nextweek()
{
	time_t start=get_start_of_week();
	int i;
	struct tm *tmptr;

	for (i=0; i<7; i++)
		nextday(&start);

	if ((tmptr=localtime(&start)) == NULL)
		return;

	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%04d%02d%02d",
	       tmptr->tm_year + 1900, tmptr->tm_mon+1, tmptr->tm_mday);

}

void sqpcp_show_cal_prevweek()
{
	time_t start=get_start_of_week();
	int i;
	struct tm *tmptr;

	for (i=0; i<7; i++)
		prevday(&start);

	if ((tmptr=localtime(&start)) == NULL)
		return;

	output_scriptptrget();
	printf("&amp;form=eventweekly&amp;weekof=%04d%02d%02d",
	       tmptr->tm_year + 1900, tmptr->tm_mon+1, tmptr->tm_mday);

}

void sqpcp_displayweek()
{
	int i;
	time_t start=get_start_of_week(), save_start;
	time_t end;
	struct PCP *pcp=sqpcp_calendar();
	std::vector<cacherecord> recs;

	if (!pcp)
		return;

	save_start=start;

	for (i=0; i<7; i++)
	{
		nextday(&start);
	}

	if (!createcache(pcp, recs, save_start, start))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	printf("<table align=\"center\" border=\"0\" class=\"weekly-border\""
	       " cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\">"
	       "<tr><td>\n");
	printf("<table border=\"1\" class=\"weekly-bg\" cellspacing=\"0\" cellpadding=\"10\" width=\"100%%\">"
	       "<tr valign=\"top\">");

	start=save_start;
	for (i=0; i<7; i++)
	{
		const char *p;
		struct tm *tmptr;

		printf("<td width=\"14%%\">");
		p=pcp_wdayname((i + pref_startofweek) % 7);
		printf("<div align=\"center\" class=\"weekly-day\">");
		printf("<a href=\"");
		output_scriptptrget();
		printf("&amp;form=eventdaily&amp;date=");

		tmptr=localtime(&start);

		if (tmptr)
		{
			printf("%04d%02d%02d", tmptr->tm_year + 1900,
			       tmptr->tm_mon+1,
			       tmptr->tm_mday);
		}
		printf("\">");
		print_safe(p);
		printf("</a><hr width=\"70%%\" />");
		printf("</div>\n");

		end=start;
		nextday(&end);


		do_daily_view(recs, VIEW_WEEKLY, &start, &end);

		start=end;
		printf("</td>");
	}
	printf("</tr></table>\n");
	printf("</td></tr></table>\n");
}

/* ---------------- Monthly view ---------------- */

static time_t get_start_of_month()
{
	unsigned y, m;
	time_t start;
	time_t end;
	struct tm *tmptr;

	if (sscanf(cgi("monthof"), "%4u%2u", &y, &m) != 2
	    || pcp_parse_ymd(y, m, 1, &start, &end))
	{
		time(&start);
		if ((tmptr=localtime(&start)) == NULL)
		{
			enomem();
			return (0);
		}
		y=tmptr->tm_year + 1900;
		m=tmptr->tm_mon + 1;

		if (pcp_parse_ymd(y, m, 1, &start, &end))
		{
			enomem();
			return (0);
		}
	}

	return (start);
}

void sqpcp_show_cal_month()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	char buf[512];

	if ((tmptr=localtime(&start)) == NULL)
		return;

	if (strftime(buf, sizeof(buf), getarg("DATEFORMAT"), tmptr) == 0)
		return;

	print_safe(buf);

}

void sqpcp_show_cal_nextmonth()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	int m, y;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	++m;
	if (m > 12)
	{
		m=1;
		++y;
	}

	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%04d%02d01", y, m);
}

void sqpcp_show_cal_prevmonth()
{
	time_t start=get_start_of_month();
	struct tm *tmptr;
	int m, y;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	--m;
	if (m <= 0)
	{
		m=12;
		--y;
	}

	output_scriptptrget();
	printf("&amp;form=eventmonthly&amp;monthof=%04d%02d01", y, m);
}

void sqpcp_displaymonth()
{
	int i, y, m;
	time_t start=get_start_of_month(), save_start;
	time_t end;
	struct PCP *pcp=sqpcp_calendar();
	std::vector<cacherecord> recs;
	struct tm *tmptr;

	if (!pcp)
		return;

	if ((tmptr=localtime(&start)) == NULL)
		return;

	y=tmptr->tm_year + 1900;
	m=tmptr->tm_mon + 1;

	if (++m > 12)
	{
		m=1;
		++y;
	}

	if (pcp_parse_ymd(y, m, 1, &end, &save_start))
		return;

	if (!createcache(pcp, recs, start, end))
	{
		show_pcp_errmsg(pcp_errmsg(pcp));
		return;
	}

	printf("<table align=\"center\" border=\"0\" class=\"monthly-border\""
	       " cellpadding=\"0\" cellspacing=\"0\" width=\"100%%\">"
	       "<tr><td>\n");
	printf("<table border=\"1\" class=\"monthly-bg\" cellspacing=\"0\" cellpadding=\"10\" width=\"100%%\">"
	       "<tr valign=\"top\">");

	for (i=0; i<7; i++)
	{
		printf("<td width=\"14%%\" align=\"center\" class=\"monthly-day\">");
		print_safe(pcp_wdayname((i + pref_startofweek) % 7));
		printf("</td>");
	}
	printf("</tr>\n");

	while (start < end)
	{
		printf("<tr valign=\"top\">\n");

		for (i=0; i<7; i++)
		{
			struct tm *tmptr;
			time_t next_day;

			tmptr=localtime(&start);

			if (tmptr->tm_wday != (i + pref_startofweek) % 7
			    || start >= end)
			{
				printf("<td width=\"14%%\" class=\"monthly-bg-othermonth\">&nbsp;</td>");
				continue;
			}

			printf("<td width=\"14%%\">");


			printf("<a href=\"");
			output_scriptptrget();
			printf("&amp;form=eventdaily&amp;date=");

			if (tmptr)
			{
				printf("%04d%02d%02d", tmptr->tm_year + 1900,
				       tmptr->tm_mon+1,
				       tmptr->tm_mday);
			}
			printf("\" class=\"monthly-day\">");

			printf("%2d", tmptr->tm_mday);
			printf("</a><div align=\"center\"><hr width=\"70%%\" /></div>\n");

			next_day=start;
			nextday(&next_day);

			do_daily_view(recs, VIEW_MONTHLY, &start,
				      &next_day);

			start=next_day;
			printf("</td>\n");
		}
		printf("</tr>\n");
	}
	printf("</table>\n");
	printf("</td></tr></table>\n");
}

/* -------------------------------------------------------------------- */
/* Access control lists */

static void addacl(std::string);

struct acl_list {
	std::string addr;
	std::string name;
	int flags{0};
} ;

static int listacl(const char *a, int f, void *vp)
{
	auto p=reinterpret_cast<std::vector<acl_list> *>(vp);

	p->push_back({a, "", f});
	return (0);
}

static int save_listacl_names(const char *addr, const char *name,
			      void *vp)
{
	auto listp=reinterpret_cast<std::vector<acl_list> *>(vp);

	for (auto &p:*listp)
		if (p.addr == addr && p.name.empty())
			p.name=name;
	return (0);
}

void sqpcp_eventacl()
{
	const char *p;
	std::vector<acl_list> acl_list;
	struct PCP *pcp;

	if (!sqpcp_has_groupware())
		return;

	p=cgi("addemail");

	while (*p && isspace((int)(unsigned char)*p))
		++p;

	if (!*p)
		p=cgi("addressbookname");

	if (*p)
		addacl(p);

	pcp=sqpcp_calendar();

	if (!pcp)
		return;

	if (*(p=cgi("remove")))
	{
		if (pcp_acl(pcp, p, 0))
		{
			saveerror(pcp, NULL);
			showerror();
		}
	}

	auto list_result=pcp_list_acl(pcp, listacl, &acl_list);

	std::sort(acl_list.begin(), acl_list.end(),
			[](const auto &a, const auto &b)
			{
				return (a.addr < b.addr);
			});

	if (list_result)
	{
		saveerror(pcp, NULL);
		showerror();
	}
	else if (ab_get_nameaddr(save_listacl_names, &acl_list))
	{
		int dummy=PCP_ERR_SYSERR;

		saveerror(NULL, &dummy);
		showerror();
	}
	else if (!acl_list.empty())
	{
		printf("<table align=\"center\">\n");

		for (auto &pp: acl_list)
		{
			printf("<tr><td align=\"right\"><span class=\"tt\">");
			if (!pp.addr.empty())
				ab_nameaddr_show(pp.name.c_str(), pp.addr.c_str());

			printf("</span></td><td>-");
			if (pp.flags & PCP_ACL_MODIFY)
				printf("&nbsp;%s", getarg("MODIFY"));
			if (pp.flags & PCP_ACL_CONFLICT)
				printf("&nbsp;%s", getarg("CONFLICT"));
			printf("</td><td><a href=\"");
			output_scriptptrget();
			printf("&amp;form=eventacl&amp;remove=");
			output_urlencoded(pp.addr.c_str());
			printf("\">%s</a></td></tr>\n", getarg("REMOVE"));
		}
		printf("</table>\n");
	}
}

static void addacl(std::string p)
{
	int flags=0;
	struct PCP *pcp;

	if (p.find('@') == std::string::npos)
	{
		const char *mhn=myhostname();

		p += '@';
		p += mhn;
	}

	if (*cgi("aclMODIFY"))
		flags |= PCP_ACL_MODIFY;

	if (*cgi("aclCONFLICT"))
		flags |= PCP_ACL_CONFLICT;

	if (!flags)
		return;	/* Noop */

	pcp=sqpcp_calendar();

	if (!pcp)
	{
		int xerror=PCP_ERR_SYSERR;

		saveerror(NULL, &xerror);
		showerror();
		return;
	}

	if (!pcp_has_acl(pcp))
	{
		printf("%s\n", getarg("NOACL"));
		return;
	}

	if (pcp_acl(pcp, p.c_str(), flags))
	{
		saveerror(pcp, NULL);
		showerror();
		return;
	}
}
