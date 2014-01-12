/*
** Copyright 1998 - 2008 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
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

#define	OLDEST1ST	"OLDEST1ST"
#define	FULLHEADERS	"FULLHEADERS"
#define	SORTORDER	"SORT"
#define	PAGESIZE	"PAGESIZE"
#define	AUTOPURGE_V	"AUTOPURGE"
#define	NOHTML		"NOHTML"
#define	FROM		"FROM"
#define	LDAP		"LDAP"
#define FLOWEDTEXT	"NOFLOWEDTEXT"
#define NOARCHIVE	"NOARCHIVE"
#define NOAUTORENAMESENT	"NOAUTORENAMESENT"
#define STARTOFWEEK	"STARTOFWEEK"
#define WIKITEXT		"WIKITEXT"

#define	OLDEST1ST_PREF	"oldest1st"
#define	FULLHEADERS_PREF "fullheaders"
#define	HTML_PREF "doshowhtml"
#define FLOWEDTEXT_PREF "noflowedtext"
#define NOARCHIVE_PREF "noarchive"
#define NOAUTORENAMESENT_PREF "noautorenamesent"

#define DEFAULTKEY	"DEFAULTKEY"

int pref_flagisoldest1st, pref_flagfullheaders;
int pref_showhtml;
int pref_flagsortorder;
int pref_flagpagesize;
int pref_autopurge;
int pref_noflowedtext;
int pref_noarchive;
int pref_noautorenamesent;
int pref_startofweek;
int pref_wikifmt;

char *pref_from=0;
char *pref_ldap=0;
extern const char *sqwebmail_content_charset;

#if ENABLE_WEBPASS
extern int check_sqwebpass(const char *);
extern void set_sqwebpass(const char *);
#endif
extern void output_attrencoded_oknl(const char *);
extern const char *sqwebmail_mailboxid;
extern void rename_sent_folder(int really);

static const char hex[]="0123456789ABCDEF";

static int nybble(char c)
{
char	*p=strchr(hex, c);

	if (p)	return (p-hex);
	return (0);
}

static void decode(char *t)
{
char *s;

	for (s=t; *s; s++)
	{
		if (*s != '+')
		{
			*t++ = *s;
			continue;
		}
		if (s[1] == 0 || s[2] == 0)
			continue;
		*t++ = nybble(s[1]) * 16 + nybble(s[2]);
		s += 2;
	}
	*t=0;
}

void pref_init()
{
const	char *p;
char	*q, *r;

	p=read_sqconfig(".", CONFIGFILE, 0);
	pref_flagisoldest1st=0;
	pref_flagfullheaders=0;
	pref_flagsortorder=0;
	pref_flagpagesize=10;
	pref_noarchive=0;

	{
		const char *autorenamesent=AUTORENAMESENT;

		const char *p=getenv("SQWEBMAIL_AUTORENAMESENT");
		if (p && *p)
			autorenamesent = p;

		pref_noautorenamesent=strncmp(autorenamesent, "no", 2) == 0;
	}

	pref_startofweek=0;
	pref_autopurge=AUTOPURGE;
	pref_showhtml=1;
	pref_wikifmt=0;

	if(pref_from) {
		free(pref_from);
		pref_from=0;
		}

	if(pref_ldap) {
		free(pref_ldap);
		pref_ldap=0;
		}


	if (p)
	{
		q=strdup(p);
		if (!q)	enomem();

		for (r=q; (r=strtok(r, " ")) != 0; r=0)
		{
			if (strcmp(r, OLDEST1ST) == 0)
				pref_flagisoldest1st=1;
			if (strcmp(r, FULLHEADERS) == 0)
				pref_flagfullheaders=1;
			if (strcmp(r, NOHTML) == 0)
				pref_showhtml=0;
			if (strcmp(r, WIKITEXT) == 0)
				pref_wikifmt=1;

			if (strncmp(r, SORTORDER, sizeof(SORTORDER)-1) == 0
				&& r[sizeof(SORTORDER)-1] == '=')
				pref_flagsortorder=r[sizeof(SORTORDER)];
			if (strncmp(r, PAGESIZE, sizeof(PAGESIZE)-1) == 0
				&& r[sizeof(PAGESIZE)-1] == '=')
				pref_flagpagesize=atoi(r+sizeof(PAGESIZE));
			if (strncmp(r, AUTOPURGE_V, sizeof(AUTOPURGE_V)-1) == 0
				&& r[sizeof(AUTOPURGE_V)-1] == '=')
				pref_autopurge=atoi(r+sizeof(AUTOPURGE_V));
			if (strncmp(r, FLOWEDTEXT, sizeof(FLOWEDTEXT)-1) == 0
				&& r[sizeof(FLOWEDTEXT)-1] == '=')
				pref_noflowedtext=atoi(r+sizeof(FLOWEDTEXT));
			if (strncmp(r, NOARCHIVE, sizeof(NOARCHIVE)-1) == 0
				&& r[sizeof(NOARCHIVE)-1] == '=')
				pref_noarchive=atoi(r+sizeof(NOARCHIVE));
			if (strncmp(r, NOAUTORENAMESENT, sizeof(NOAUTORENAMESENT)-1) == 0
				&& r[sizeof(NOAUTORENAMESENT)-1] == '=')
				pref_noautorenamesent=atoi(r+sizeof(NOAUTORENAMESENT));
			if (strncmp(r, FROM, sizeof(FROM)-1) == 0
				&& r[sizeof(FROM)-1] == '=')
			{
				if (pref_from)	free(pref_from);
				if ((pref_from=strdup(r+sizeof(FROM))) == 0)
					enomem();

				decode(pref_from);
			}
			if (strncmp(r, LDAP, sizeof(LDAP)-1) == 0
				&& r[sizeof(LDAP)-1] == '=')
			{
				if (pref_ldap)	free(pref_ldap);
				if ((pref_ldap=strdup(r+sizeof(LDAP))) == 0)
					enomem();

				decode(pref_ldap);
			}
			if (strncmp(r, STARTOFWEEK, sizeof(STARTOFWEEK)-1) == 0
				&& r[sizeof(STARTOFWEEK)-1] == '=')
			{
				int n=atoi(r+sizeof(STARTOFWEEK));

				if (n >= 0 && n < 7)
					pref_startofweek=n;
			}

		}
		free(q);
	}
	switch (pref_flagpagesize)	{
	case 20:
	case 50:
	case 100:
	case 250:
		break;
	default:
		pref_flagpagesize=10;
		break;
	}

	if (pref_autopurge < 0)	pref_autopurge=0;
	if (pref_autopurge > MAXPURGE)	pref_autopurge=MAXPURGE;

	switch (pref_flagsortorder)	{
	case 'F':
	case 'S':
		break;
	default:
		pref_flagsortorder='D';
		break;
	}
}

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

static char *append_str(const char *prefs, const char *label,
	const char *value)
{
int	l=strlen(prefs) + sizeof(" =") +
	strlen(label)+ (value ? strlen(value):0);
int	i;
char	*p;
const char *q;

	for (i=0; value && value[i]; i++)
		if (value[i] <= ' ' || value[i] >= 127
			|| value[i] == '+')
			l += 2;

	p=malloc(l);
	if (!p)	enomem();
	strcpy(p, prefs);
	if (!value || !*value)	return (p);

	strcat(strcat(strcat(p, " "), label), "=");
	i=strlen(p);
	for (q=value; *q; q++)
	{
		if (*q <= ' ' || *q >= 127 || *q == '+')
		{
			sprintf(p+i, "+%02X", (int)(unsigned char)*q);
			i += 3;
			continue;
		}
		p[i++]= *q;
	}
	p[i]=0;
	return (p);
}

void pref_update()
{
char	buf[1000];
char	*p;
char	*q;

	sprintf(buf, SORTORDER "=%c " PAGESIZE "=%d " AUTOPURGE_V "=%d "
		FLOWEDTEXT "=%d " NOARCHIVE "=%d " NOAUTORENAMESENT "=%d "
		STARTOFWEEK "=%d",
		pref_flagsortorder, pref_flagpagesize, pref_autopurge,
		pref_noflowedtext, pref_noarchive, pref_noautorenamesent,
		pref_startofweek);

	if (pref_flagisoldest1st)
		strcat(buf, " " OLDEST1ST);

	if (pref_flagfullheaders)
		strcat(buf, " " FULLHEADERS);

	if (!pref_showhtml)
		strcat(buf, " " NOHTML);

	if (pref_wikifmt)
		strcat(buf, " " WIKITEXT);

	p=append_str(buf, FROM, pref_from);
	q=append_str(p, LDAP, pref_ldap);
	write_sqconfig(".", CONFIGFILE, q);
	free(q);
	free(p);
}

void pref_setfrom(const char *p)
{
	if (pref_from)	free(pref_from);
	pref_from=strdup(p);
	if (!pref_from)	enomem();
	pref_update();
}

void pref_setldap(const char *p)
{
	if (pref_ldap && strcmp(p, pref_ldap) == 0)
		return;

	if (pref_ldap)	free(pref_ldap);
	pref_ldap=strdup(p);
	if (!pref_ldap)	enomem();
	pref_update();
}

void pref_setprefs()
{
	if (*cgi("do.changeprefs"))
	{
	char	buf[1000];
	FILE	*fp;
	char	*p;
	char	*q;

		sprintf(buf, SORTORDER "=%c " PAGESIZE "=%s " AUTOPURGE_V "=%s "
			FLOWEDTEXT "=%s "
			NOARCHIVE "=%s "
			NOAUTORENAMESENT "=%s "
			STARTOFWEEK "=%d",
			*cgi("sortorder"), cgi("pagesize"), cgi("autopurge"),
			*cgi(FLOWEDTEXT_PREF) ? "1":"0",
			*cgi(NOARCHIVE_PREF) ? "1":"0",
			*cgi(NOAUTORENAMESENT_PREF) ? "1":"0",
			(int)((unsigned)atoi(cgi(STARTOFWEEK)) % 7)
			);

		if (*cgi(OLDEST1ST_PREF))
			strcat(buf, " " OLDEST1ST);
		if (*cgi(FULLHEADERS_PREF))
			strcat(buf, " " FULLHEADERS);
		if (!*cgi(HTML_PREF))
			strcat(buf, " " NOHTML);

		p=append_str(buf, FROM, pref_from);
		q=append_str(p, LDAP, pref_ldap);
		write_sqconfig(".", CONFIGFILE, q);
		free(p);
		free(q);
		pref_init();
		if ((fp=fopen(SIGNATURE, "w")) != NULL)
		{
			char *sig_utf8=
				unicode_convert_toutf8(cgi("signature"),
							 sqwebmail_content_charset,
							 NULL);

			if (sig_utf8)
			{
				fprintf(fp, "%s", sig_utf8);
				free(sig_utf8);
			}
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
				login_changepwd(sqwebmail_mailboxid,
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

	printf("<select name=\"" STARTOFWEEK "\">");

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

char *pref_getsig()
{
	return pref_getfile(fopen(SIGNATURE, "r"));
}

char *pref_getfile(FILE *fp)
{
	struct stat st;
	char *utf8_buf;
	char *sig_buf;

	if (fp == NULL)
		return NULL;

	if (fstat(fileno(fp), &st) < 0)
	{
		fclose(fp);
		return NULL;
	}

	utf8_buf=malloc(st.st_size+1);

	if (!utf8_buf)
	{
		fclose(fp);
		return NULL;
	}

	if (fread(utf8_buf, 1, st.st_size, fp) != st.st_size)
	{
		fclose(fp);
		return NULL;
	}
	utf8_buf[st.st_size]=0;
	fclose(fp);

	sig_buf=unicode_convert_fromutf8(utf8_buf,
					   sqwebmail_content_charset,
					   NULL);
	free(utf8_buf);

	return sig_buf;
}

void pref_signature()
{
	char *p=pref_getsig();

	if (p)
	{
		output_attrencoded_oknl(p);
		free(p);
	}
}
/*
** Get a setting from GPGCONFIGFILE
**
** GPGCONFIGFILE consists of space-separated settings.
*/

static char *getgpgconfig(const char *name)
{
	const char *p;
	char	*q, *r;

	int name_l=strlen(name);

	p=read_sqconfig(".", GPGCONFIGFILE, 0);

	if (p)
	{
		q=strdup(p);
		if (!q)
			enomem();

		for (r=q; (r=strtok(r, " ")) != NULL; r=NULL)
			if (strncmp(r, name, name_l) == 0 &&
			    r[name_l] == '=')
			{
				r=strdup(r+name_l+1);
				free(q);
				if (!r)
					enomem();
				return (r);
			}
		free(q);
	}
	return (NULL);
}

/*
** Enter a setting into GPGCONFIGFILE
*/

static void setgpgconfig(const char *name, const char *value)
{
	const	char *p;
	char *q, *r, *s;
	int name_l=strlen(name);

	/* Get the existing settings */

	p=read_sqconfig(".", GPGCONFIGFILE, 0);

	if (!p)
		p="";

	q=strdup(p);
	if (!q)
		enomem();

	s=malloc(strlen(q)+strlen(name)+strlen(value)+4);
	if (!s)
		enomem();
	*s=0;

	/*
	** Copy existing settings into a new buffer, deleting any old
	** setting.
	*/

	for (r=q; (r=strtok(r, " ")) != NULL; r=NULL)
	{
		if (strncmp(r, name, name_l) == 0 &&
		    r[name_l] == '=')
		{
			continue;
		}

		if (*s)
			strcat(s, " ");
		strcat(s, r);
	}

	/* Append the new setting */

	if (*s)
		strcat(s, " ");
	strcat(strcat(strcat(s, name), "="), value);
	free(q);
	write_sqconfig(".", GPGCONFIGFILE, s);
	free(s);
}

char *pref_getdefaultgpgkey()
{
	return (getgpgconfig(DEFAULTKEY));
}

void pref_setdefaultgpgkey(const char *v)
{
	setgpgconfig(DEFAULTKEY, v);
}
