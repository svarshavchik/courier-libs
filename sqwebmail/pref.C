/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"pref.h"
#include	"config.h"
#include	"auth.h"
#include	"sqwebmail.h"
#include	"sqconfig.h"
#include	"mailinglist.h"
#include	"cgi/cgi.h"
#include	"pcp/pcp.h"
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/stat.h>
#include	<unistd.h>
#include	<charconv>

#define	OLDEST1ST_PREF	"oldest1st"
#define	FULLHEADERS_PREF "fullheaders"
#define	HTML_PREF "doshowhtml"
#define FLOWEDTEXT_PREF "noflowedtext"
#define NOARCHIVE_PREF "noarchive"
#define NOAUTORENAMESENT_PREF "noautorenamesent"

#define DEFAULTKEY	"DEFAULTKEY"
#define STARTOFWEEK_PREF "STARTOFWEEK"


#if ENABLE_WEBPASS
extern int check_sqwebpass(const char *);
extern void set_sqwebpass(const char *);
#endif
extern void output_attrencoded_oknl(const char *);
extern std::string sqwebmail_mailboxid;
extern void rename_sent_folder(int really);

#if 0
#if ENABLE_WEBPASS
static int goodpass(const char *p)
{
	for ( ; *p; p++)
		if (*p < ' ')	return (0);
	return (1);
}
#endif
#endif

void pref_setfrom(std::string p)
{
	pref_from=std::move(p);
	pref_update();
}

void pref_setldap(const char *p)
{
	if (pref_ldap == p)
		return;

	pref_ldap=p;
	pref_update();
}

void pref_setprefs()
{
	if (*cgi("do.changeprefs"))
	{
		pref_flagsortorder=*cgi("sortorder");
		pref_flagpagesize=*cgi("pagesize");
		pref_autopurge=*cgi("autopurge");
		pref_noflowedtext=*cgi(FLOWEDTEXT_PREF) ? 1:0;
		pref_noarchive=*cgi(NOARCHIVE_PREF) ? 1:0;
		pref_noautorenamesent=*cgi(NOAUTORENAMESENT_PREF) ? 1:0;
		pref_startofweek=*cgi(STARTOFWEEK_PREF);
		pref_flagisoldest1st=*cgi(OLDEST1ST_PREF);
		pref_flagfullheaders=*cgi(FULLHEADERS_PREF);
		pref_showhtml=*cgi(HTML_PREF);
		pref_update();
		pref_init();

		FILE *fp;

		if ((fp=fopen(SIGNATURE, "w")) != NULL)
		{
			auto s=unicode::iconvert::convert(
					cgi("signature"),
					sqwebmail_content_charset,
					unicode::utf_8);

			if (s.size())
				fprintf(fp, "%s", s.c_str());
			fclose(fp);
		}

		savemailinglists(cgi("mailinglists"));

		printf("%s\n", getarg("PREFSOK"));

		rename_sent_folder(0);
	}

	if (*cgi("do.changepwd"))
	{
		int status=1;
		const	char *p=cgi("newpass");
		int has_syspwd=0;

		if ( *p && strcmp(p, cgi("newpass2")) == 0
			&& strlen(p) >= MINPASSLEN)
		{
			has_syspwd=
				login_changepwd(sqwebmail_mailboxid.c_str(),
						cgi("oldpass"), p,
						&status);
		}

		if (has_syspwd || status)
		{
			printf("%s\n", getarg("PWDERR"));
		}
		else
		{
			printf("%s\n", getarg("PWDOK"));
		}
	}

#if 0
	if (*cgi("do.changepwd"))
	{
#if ENABLE_WEBPASS

	const	char *p;

		if (check_sqwebpass(cgi("oldpass")) == 0 &&
			*(p=cgi("newpass")) &&
			goodpass(p) &&
			strcmp(p, cgi("newpass2")) == 0)
		{
			set_sqwebpass(p);
		}
		else
		{
			printf("%s\n", getarg("PWDERR"));
		}
#else
		printf("%s\n", getarg("PWDERR"));
#endif
	}
#endif
}

void pref_isoldest1st()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		OLDEST1ST_PREF, OLDEST1ST_PREF, pref_flagisoldest1st ? " checked=\"checked\"":"");
}

void pref_isdisplayfullmsg()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		FULLHEADERS_PREF, FULLHEADERS_PREF, pref_flagfullheaders ? " checked=\"checked\"":"");
}

void pref_displayhtml()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		HTML_PREF, HTML_PREF, pref_showhtml ? " checked=\"checked\"":"");
}

void pref_displayflowedtext()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		FLOWEDTEXT_PREF, FLOWEDTEXT_PREF, pref_noflowedtext ? " checked=\"checked\"":"");
}

void pref_displayweekstart()
{
	int i, j;
	static const int d[3]={6,0,1};

	printf("<select name=\"" STARTOFWEEK_PREF "\">");

	for (j=0; j<3; j++)
	{
		i=d[j];
		printf("<option value=\"%d\"%s>", i,
		       i == pref_startofweek ? " selected='selected'":"");

		output_attrencoded_oknl( pcp_wdayname_long(i));
		printf("</option>");
	}
	printf("</select>");
}

void pref_displaynoarchive()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		NOARCHIVE_PREF, NOARCHIVE_PREF, pref_noarchive ? " checked=\"checked\"":"");
}

void pref_displaynoautorenamesent()
{
	printf("<input type=\"checkbox\" name=\"%s\" id=\"%s\"%s />",
		NOAUTORENAMESENT_PREF, NOAUTORENAMESENT_PREF, pref_noautorenamesent ? " checked=\"checked\"":"");
}

void pref_displayautopurge()
{
	printf("<input type=\"text\" name=\"autopurge\" value=\"%d\" size=\"2\" maxlength=\"2\" />",
		pref_autopurge);
}

void pref_sortorder()
{
static const char selected[]=" selected='selected'";

	printf("<select name=\"sortorder\">");
	printf("<option value=\"DATE\"%s>%s</option>\n",
		pref_flagsortorder == 'D' ? selected:"",
	       getarg("DATE"));

	printf("<option value=\"FROM\"%s>%s</option>\n",
		pref_flagsortorder == 'F' ? selected:"",
	       getarg("SENDER"));
	printf("<option value=\"SUBJECT\"%s>%s</option>\n",
		pref_flagsortorder == 'S' ? selected:"",
	       getarg("SUBJECT"));
	printf("</select>\n");
}

void pref_pagesize()
{
static const char selected[]=" selected='selected'";

	printf("<select name=\"pagesize\">");
	printf("<option value=\"10\"%s>10</option>\n",
		pref_flagpagesize == 10 ? selected:"");
	printf("<option value=\"20\"%s>20</option>\n",
		pref_flagpagesize == 20 ? selected:"");
	printf("<option value=\"50\"%s>50</option>\n",
		pref_flagpagesize == 50 ? selected:"");
	printf("<option value=\"100\"%s>100</option>\n",
		pref_flagpagesize == 100 ? selected:"");
	printf("<option value=\"250\"%s>250</option>\n",
		pref_flagpagesize == 250 ? selected:"");
	printf("</select>\n");
}

std::string pref_getsig()
{
	return pref_getfile(fopen(SIGNATURE, "r"));
}

std::string pref_getfile(FILE *fp)
{
	struct stat st;

	if (fp == NULL)
		return "";

	if (fstat(fileno(fp), &st) < 0)
	{
		fclose(fp);
		return "";
	}

	std::string utf8_buf;
	utf8_buf.reserve(st.st_size);

	int c;
	while ((c=fgetc(fp)) != EOF)
		utf8_buf.push_back(static_cast<char>(c));
	fclose(fp);

	std::string sig_buf=unicode::iconvert::convert(
		utf8_buf,
		unicode::utf_8,
		sqwebmail_content_charset
	);

	return sig_buf;
}

void pref_signature()
{
	std::string p=pref_getsig();

	output_attrencoded_oknl(p.c_str());
}
/*
** Get a setting from GPGCONFIGFILE
**
** GPGCONFIGFILE consists of space-separated settings.
*/

static std::string getgpgconfig(std::string_view name)
{
	auto p=read_sqconfig(".", GPGCONFIGFILE, 0);

	if (p && p->size())
	{
		std::string_view q=*p;

		while (q.size())
		{
			auto r=q.find(' ');
			if (r == std::string_view::npos)
				r=q.size();
			auto w=q.substr(0, r);

			if (r == q.size())
				q=std::string_view{};
			else
				q=q.substr(r+1);

			if (w.size() > name.size() &&
			    w.substr(0, name.size()) == name &&
			    w[name.size()] == '=')
			{
				auto value=w.substr(name.size()+1);
				return std::string{value.begin(), value.end()};
			}
		}
	}
	return {};
}

/*
** Enter a setting into GPGCONFIGFILE
*/

static void setgpgconfig(std::string_view name, std::string_view value)
{
	/* Get the existing settings */

	auto p=read_sqconfig(".", GPGCONFIGFILE, 0);

	if (!p)
		p="";

	std::string newconfig;
	newconfig.reserve(p->size()+name.size()+value.size()+4);

	/*
	** Copy existing settings into a new buffer, deleting any old
	** setting.
	*/
	std::string_view q=*p;

	while (q.size())
	{
		auto r=q.find(' ');
		if (r == std::string_view::npos)
			r=q.size();
		auto w=q.substr(0, r);

		if (r == q.size())
			q=std::string_view{};
		else
			q=q.substr(r+1);

		size_t equals=w.find('=');
		if (equals == std::string_view::npos)
			continue;

		if (w.substr(0, equals) == name)
			continue;

		if (!newconfig.empty())
			newconfig+=' ';
		newconfig+=w;
	}

	/* Append the new setting */

	if (!newconfig.empty())
		newconfig+=' ';
	newconfig+=name;
	newconfig+='=';
	newconfig+=value;

	write_sqconfig(".", GPGCONFIGFILE, newconfig.c_str());
}

std::string pref_getdefaultgpgkey()
{
	return (getgpgconfig(DEFAULTKEY));
}

void pref_setdefaultgpgkey(const char *v)
{
	setgpgconfig(DEFAULTKEY, v);
}
