#include "config.h"
/*
** Copyright 1998 - 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/*
*/
#include	"sqwebmail.h"
#include	"maildir.h"
#include	"folder.h"
#include	"cgi/cgi.h"
#include	"rfc2045/rfc2045.h"
#include	"maildir/maildirmisc.h"
#include	"buf.h"
#include	"ispell.h"
#include	"filter.h"
#include	"newmsg.h"
#include	<stdio.h>
#include	<string.h>
#include	<fcntl.h>
#include	<ctype.h>

extern const char *sqwebmail_content_charset;
extern void output_form(const char *);
extern const char *sqwebmail_content_ispelldict;
extern void output_attrencoded(const char *);

static void spelladd(const char *);
static int search_spell(const char *, unsigned, unsigned);

int spell_start(const char *c)
{
char	*filename=maildir_find(INBOX "." DRAFTS, c);

	if (!c)	return (-1);

	if (search_spell(filename, 0, 0) == 0)
		return (-1);

	return (0);
}

/*
** Search for misspelled words.
*/

static struct rfc2045 *findtext(struct rfc2045 *);

static char *spell_check(const char *, unsigned, unsigned,
	const char *, const char *, const char *, int *);

static int search_spell(const char *filename, unsigned parnum, unsigned pos)
{
struct	rfc2045	*rfcp, *textp;
struct	buf newtext, current_line;
off_t start_pos, end_pos, start_body;
int	made_replacements, has_misspelling;
char *new_line;
unsigned paragraph;
const char	*ignoreword="";
const char	*replacefrom="";
const char	*replaceto="";
int	checked=0;
off_t	dummy;
FILE	*fp=0;
int	x;

	x=maildir_safeopen(filename, O_RDONLY, 0);
	if (x >= 0)
		if ((fp=fdopen(x, "r")) == 0)
			close(x);

	if (!fp)	return (0);
	rfcp=rfc2045_fromfp(fp);
	if (!rfcp)	enomem();

	textp=findtext(rfcp);

	if (!textp)
	{
		rfc2045_free(rfcp);
		fclose(fp);
		return (0);
	}

	buf_init(&newtext);
	buf_init(&current_line);

        rfc2045_mimepos(textp, &start_pos, &end_pos, &start_body,
		&dummy, &dummy);
        if (fseek(fp, start_body, SEEK_SET) == -1)
                enomem();

	made_replacements=0;
	has_misspelling=0;
	paragraph=0;
        for ( ; start_body < end_pos; start_body++)
	{
	int	c=getc(fp);

		if (c < 0)	enomem();
		if (c != '\n')
		{
			buf_append(&current_line, c);
			continue;
		}
		buf_append(&current_line, '\0');
		if (parnum)
		{
			--parnum;
			buf_cat(&newtext, current_line.ptr);
			buf_cat(&newtext, "\n");
			current_line.cnt=0;
			++paragraph;
			continue;
		}

		if (!checked)
		{
		int	l;

			checked=1;
			if ((l=strlen(cgi("word"))) > 0)
			{

/* Ok, what should we do? */

			const char *newword=cgi("REPLACE");

				if (!*newword || strcmp(newword, "#other") == 0)
					newword=cgi("OTHER");
				/*
				** Perhaps they entered the word without
				** checking this checkmark.
				*/
				else if (*newword == '#')
					newword="";

				if (*newword && pos + l <= strlen(current_line.ptr))
				{
				struct buf tempbuf;

					buf_init(&tempbuf);
					buf_cpyn(&tempbuf, current_line.ptr,
						pos);
					buf_cat(&tempbuf, newword);
					buf_cat(&tempbuf,
						current_line.ptr+pos+l);
					pos += strlen(newword);
					if (*cgi("REPLACEALL"))
					{
						replacefrom=cgi("word");
						replaceto=newword;
					}
					buf_append(&tempbuf, '\0');
					buf_cpy(&current_line, tempbuf.ptr);
					buf_append(&current_line, '\0');
					buf_free(&tempbuf);
					made_replacements=1;
				}
				else
				{
					pos += l;
					if (strcmp(cgi("REPLACE"),
						"#ignoreall") == 0)
						ignoreword=cgi("word");
				}

				if (strcmp(cgi("REPLACE"),
						"#insert") == 0)
				{
					spelladd(cgi("word"));
				}
			}
		}


		if (*current_line.ptr == '>')
		{
			buf_cat(&newtext, current_line.ptr);
			buf_cat(&newtext, "\n");
			pos=0;
			current_line.cnt=0;
			++paragraph;
			continue;
		}
		if (!has_misspelling)
		{
			new_line=spell_check(current_line.ptr, paragraph, pos,
				ignoreword, replacefrom, replaceto,
				&has_misspelling);
			if (new_line)
			{
				buf_cat(&newtext, new_line);
				free(new_line);
				made_replacements=1;
			}
			else	buf_cat(&newtext, current_line.ptr);
		}
		else	buf_cat(&newtext, current_line.ptr);
		buf_cat(&newtext, "\n");
		pos=0;
		current_line.cnt=0;
		++paragraph;
	}
	if (current_line.cnt)
		buf_cat(&newtext, "\n");
	rfc2045_free(rfcp);
	fclose(fp);
	if (made_replacements)
	{
	char	*p=newmsg_createdraft_do(filename, newtext.ptr,
					 NEWMSG_SQISPELL);

		if (p)	free(p);

		if (*cgi("error"))
		{
			has_misspelling=0;	/* Abort spell checking */
		}
	}

	buf_free(&newtext);
	buf_free(&current_line);

	if (*ignoreword)
	{
	static char *p=0;

		if (p)	free(p);
		p=malloc(strlen(cgi("globignore")) + 2 + strlen(ignoreword));

		if (!p)	enomem();

		strcpy(p, cgi("globignore"));
		if (*p)	strcat(p, ":");
		strcat(p, ignoreword);
		cgi_put("globignore", p);
	}

	if (*replacefrom)
	{
	static char *p=0;

		if (p)	free(p);
		p=malloc(strlen(cgi("globreplace"))+3
			+strlen(replacefrom)+strlen(replaceto));

		if (!p)	enomem();
		strcpy(p, cgi("globreplace"));
		if (*p)	strcat(p, ":");
		strcat(strcat(strcat(p, replacefrom), ":"), replaceto);
		cgi_put("globreplace", p);
		free(p);
	}
	if (has_misspelling)	return (1);
	return (0);
}

static struct rfc2045 *findtext(struct rfc2045 *rfcp)
{
struct rfc2045 *textp;
const char *content_type;
const char *content_transfer_encoding;
const char *charset;

	rfc2045_mimeinfo(rfcp, &content_type,
		&content_transfer_encoding, &charset);
	if (strncmp(content_type, "text/", 5) == 0)
		textp=rfcp;
	else
	{
		for (textp=rfcp->firstpart; textp; textp=textp->next)
		{
			if (textp->isdummy)	continue;
			rfc2045_mimeinfo(textp, &content_type,
				&content_transfer_encoding, &charset);
			if (strncmp(content_type, "text/", 5) == 0)
				break;
		}
	}
	return (textp);
}

/*
** Ok, check a single paragraph, starting at position #pos.
**
** If some replacements were made due to previous saved 'replace all' words,
** return the text of the modified line.  Otherwise return NULL.
**
** Set *hasmisspelled to 1 if there are some misspellings in this line.
*/

static struct	ispell	*ispellptr;
static char *ispellline=0;
static unsigned paragraph;

static int spellignore(const char *);
static char *spellreplace(const char *);

static char *spell_check(const char *line, unsigned pnum, unsigned pos,
	const char *ignoreword,
	const char *replacefrom,
	const char *replaceto,
	int *hasmisspelled)
{
struct ispell_misspelled	*msp, *np;
char	*newline=0;
const char *newword;
char	*w;

	if (strlen(line) <= pos)	return (0);	/* Sanity check */

	ispellptr=ispell_run(sqwebmail_content_ispelldict, line+pos);
	if (!ispellptr)	enomem();
	for (msp=ispellptr->first_misspelled; msp; msp=msp->next)
		if (msp->misspelled_word)
			msp->word_pos += pos;

	for (msp=ispellptr->first_misspelled; msp; msp=msp->next)
	{
		if ((*ignoreword &&
			strcmp(msp->misspelled_word, ignoreword) == 0)
			|| spellignore(msp->misspelled_word))
		{
			msp->misspelled_word=0;
			continue;
		}

		newword=0;
		if ( *replacefrom &&
			strcmp(msp->misspelled_word, replacefrom) == 0)
			newword=replaceto;

		w=0;
		if (newword ||
			(newword=w=spellreplace(msp->misspelled_word)) != 0)
		{
		char	*p=malloc(strlen(newline ? newline:line)+strlen(newword)+1);

			if (!p)	enomem();
			memcpy(p, (newline ? newline:line), msp->word_pos);
			strcpy(p+msp->word_pos, newword);
			strcat(p, (newline ? newline:line)+msp->word_pos+
				strlen(msp->misspelled_word));
			if (newline)	free(newline);
			newline=p;
			for (np=msp; (np=np->next) != 0; )
				np->word_pos += strlen(newword)-strlen(msp->misspelled_word);
			msp->misspelled_word=0;
			if (w)
				free(w);
			continue;
		}
		*hasmisspelled=1;
		paragraph=pnum;
		break;
	}
	if (!hasmisspelled)
	{
		ispell_free(ispellptr);
		ispellptr=0;
	}
	else
	{
		if (ispellline)	free(ispellline);
		if ((ispellline=malloc(strlen( newline ? newline:line)+1)) == 0)
			enomem();
		strcpy(ispellline, newline ? newline:line);
	}
	return (newline);
}

static void showfunc(const char *p, size_t n, void *dummy)
{
	while (n)
	{
		if (*p == ' ')
			printf("&nbsp;");
		else if (*p != '\n')
			putchar(*p);
		p++;
		--n;
	}
}

static void show_part(const char *ptr, size_t cnt)
{
	char32_t *uc;
	size_t ucsize;
	int conv_err;

	if (unicode_convert_tou_tobuf(ptr, cnt,
					sqwebmail_content_charset,
					&uc,
					&ucsize,
					&conv_err) == 0)
	{
		if (conv_err)
		{
			free(uc);
			uc=NULL;
		}
	}


	if (uc)
	{

		struct filter_info info;

		filter_start(&info, sqwebmail_content_charset,
			     &showfunc, NULL);
		filter(&info, uc, ucsize);
		filter_end(&info);

		free(uc);
	}
}

void spell_show()
{
const char *draftmessage=cgi("draftmessage");
struct ispell_misspelled *msp;
struct ispell_suggestion *isps;
size_t	p, l=strlen(ispellline), n;
const char *ignorelab=getarg("IGNORE");
const char *ignorealllab=getarg("IGNOREALL");
const char *replacelab=getarg("REPLACE");
const char *replacealllab=getarg("REPLACEALL");
const char *insertlab=getarg("INSERT");
const char *continuelab=getarg("CONTINUE");
const char *finishlab=getarg("FINISH");

	if (!ispellptr)	enomem();

	if (!ignorelab)		ignorelab="";
	if (!ignorealllab)	ignorealllab="";
	if (!replacelab)	replacelab="";
	if (!replacealllab)	replacealllab="";
	if (!continuelab)	continuelab="";
	if (!finishlab)		finishlab="";

	for (msp=ispellptr->first_misspelled; msp; msp=msp->next)
		if (msp->misspelled_word)	break;
	if (!msp)	enomem();

	CHECKFILENAME(draftmessage);

	printf("<input type=\"hidden\" name=\"form\" value=\"spellchk\" />\n");
	printf("<input type=\"hidden\" name=\"pos\" value=\"%s\" />\n", cgi("pos"));
	if (*cgi("globignore"))
	{
		printf("<input type=\"hidden\" name=\"globignore\" value=\"");
		output_attrencoded(cgi("globignore"));
		printf("\" />\n");
	}
	if (*cgi("globreplace"))
	{
		printf("<input type=\"hidden\" name=\"globreplace\" value=\"");
		output_attrencoded(cgi("globreplace"));
		printf("\" />\n");
	}

	printf("<input type=\"hidden\" name=\"draftmessage\" value=\"");
	output_attrencoded(draftmessage);
	printf("\" />");
	printf("<input type=\"hidden\" name=\"row\" value=\"%u\" /><input type=\"hidden\" name=\"col\" value=\"%u\" /><input type=\"hidden\" name=\"word\" value=\"",
		(unsigned)paragraph,
		(unsigned)msp->word_pos);
	output_attrencoded(msp->misspelled_word);

	printf("\" /><table border=\"0\" cellspacing=\"0\" cellpadding=\"1\" "
	       "class=\"box-small-outer\"><tr><td>");
	printf("<table border=\"0\" cellspacing=\"0\" class=\"spellcheck-background\"><tr><td>");

	printf("<table border=\"1\" cellspacing=\"0\" cellpadding=\"8\" class=\"spellcheck-excerpt\"><tr><td align=\"center\"><span style=\"color: #000000\" class=\"spellcheck-excerpt\">");

	if (msp->word_pos > 30)
	{
		p=msp->word_pos-30;
		for (n=p; n<msp->word_pos; n++)
			if (ispellline[n] == ' ')
			{
				while (n < p && ispellline[n] == ' ')
					++n;
				p=n;
				break;
			}
		printf("...&nbsp;");
	}
	else
		p=0;


	show_part(ispellline+p, msp->word_pos-p);
	printf("<strong>");
	show_part(ispellline+msp->word_pos, strlen(msp->misspelled_word));
	printf("</strong>");

	p=msp->word_pos+strlen(msp->misspelled_word);
	if (l-p < 30)
	{
		n=l-p;
	}
	else	n=30;

	while (n)
	{
		if (ispellline[n+p] != ' ')
		{
			--n;
			continue;
		}
		while (n && ispellline[n+p-1] == ' ')
			--n;
		break;
	}

	show_part(ispellline+p, n);

	if (n != l-p)
		printf("&nbsp;...");
	printf("</span></td></tr></table><br />");
	printf("<table border=\"1\" cellpadding=\"8\" class=\"spellcheck-main\"><tr><td>");

	printf("<table border=\"0\">");
	for (isps=msp->first_suggestion; isps; isps=isps->next)
	{
		printf("<tr><td>%s</td><td><input type=\"radio\" name=\"REPLACE\" value=\"%s\" /></td><td>%s</td></tr>\n",
			replacelab,
			isps->suggested_word,
			isps->suggested_word);
		replacelab=" ";
	}
	printf("<tr><td>%s</td><td><input type=\"radio\" name=\"REPLACE\" value=\"#other\" /></td><td><input type=\"text\" name=\"OTHER\" size=\"20\" /></td></tr>\n",
		replacelab);
	printf("<tr><td> </td><td><input type=\"radio\" name=\"REPLACE\" value=\"#insert\" /></td><td>%s</td></tr>\n",
		insertlab);

	printf("<tr><td> </td><td><input type=\"checkbox\" name=\"REPLACEALL\" /></td><td>%s</td></tr>\n",
		replacealllab);
	printf("<tr><td> </td><td colspan=\"2\"><hr width=\"100%%\" /></td></tr>\n");
	printf("<tr><td> </td><td><input type=\"radio\" name=\"REPLACE\" value=\"#ignore\" /></td><td>%s</td></tr>\n",
		ignorelab);
	printf("<tr><td> </td><td><input type=\"radio\" name=\"REPLACE\" value=\"#ignoreall\" /></td><td>%s</td></tr>\n",
		ignorealllab);
	printf("</table>");
	printf("</td></tr></table><br />");
	printf("<table border=\"1\" cellpadding=\"8\" class=\"spellcheck-continue\"><tr><td>");
	printf("<input type=\"submit\" name=\"continue\" value=\"%s\" />\n",
			continuelab);
	printf("<input type=\"submit\" name=\"finish\" value=\"%s\" />\n",
			finishlab);
	printf("</td></tr></table>\n");
	printf("</td></tr></table>\n");
	printf("</td></tr></table>\n");
}

static FILE *opendict(const char *mode)
{
FILE	*fp;
char	*p=malloc(sqwebmail_content_ispelldict ?
			strlen(sqwebmail_content_ispelldict)+20:20);

	if (!p)	enomem();
	strcat(strcpy(p, sqwebmail_content_ispelldict ?
			"sqwebmail-dict-":"sqwebmail-dict"),
		sqwebmail_content_ispelldict ? sqwebmail_content_ispelldict:"");
	fp=fopen(p, mode);
	free(p);
	return (fp);
}

static int spellignore(const char *word)
{
char	buf[100];
const char *c;
char	*p, *q;
FILE	*fp=opendict("r");

	if (!fp)	return (0);
	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		if ((p=strchr(buf, '\n')) != 0)	*p=0;
		if (strcmp(word, buf) == 0)
		{
			fclose(fp);
			return (1);
		}
	}
	fclose(fp);

	c=cgi("globignore");

	p=malloc(strlen(c)+1);
	if (!p)	enomem();
	strcpy(p, c);

	for (q=p; (q=strtok(q, ":")) != 0; q=0)
		if (strcmp(q, word) == 0)
		{
			free(p);
			return (1);
		}

	return (0);
}

static void spelladd(const char *word)
{
FILE	*fp=opendict("a");

	if (fp)
	{
		fprintf(fp, "%s\n", word);
		fclose(fp);
	}
}

static char *spellreplace(const char *word)
{
char	*p, *q, *r;
const char *c=cgi("globreplace");

	p=malloc(strlen(c)+1);
	if (!p)	enomem();
	strcpy(p, c);
	for (q=p; (q=strtok(q, ":")) != 0 && (r=strtok(0, ":")) != 0; q=0)
	{
		if (strcmp(q, word) == 0)
		{
			q=malloc(strlen(r)+1);
			if (!q)	enomem();
			strcpy(q, r);
			free(p);
			return (q);
		}
	}
	free(p);
	return (0);
}

void spell_check_continue()
{
const char *filename=cgi("draftmessage");
unsigned parnum=atol(cgi("row"));
unsigned pos=atol(cgi("col"));
char	*draftfilename;

	CHECKFILENAME(filename);
	draftfilename=maildir_find(INBOX "." DRAFTS, filename);
	if (!draftfilename)
	{
		output_form("folder.html");
		return;
	}

	if (search_spell(draftfilename, parnum, pos) &&
		*cgi("continue"))
		output_form("spellchk.html");
	else
	{
		cgi_put("draft", cgi("draftmessage"));
		cgi_put("previewmsg","SPELLCHK");
		output_form("newmsg.html");
	}
	free(draftfilename);
}

void ispell_cleanup()
{
	if(ispellline) free(ispellline);
	ispellline=NULL;
}
