#include "config.h"
/*
** Copyright 1998 - 2001 S. Varshavchik.  See COPYING for
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
#include	"ispell.h"
#include	"filter.h"
#include	"newmsg.h"
#include	<stdio.h>
#include	<string.h>
#include	<fcntl.h>
#include	<ctype.h>
#include	<string>
#include	<string_view>
#include	<optional>
#include	<fstream>

extern void output_form(const char *);
extern void output_attrencoded(std::string_view);

static void spelladd(const char *);
static bool search_spell(const char *, unsigned, unsigned);

int spell_start(const char *c)
{
	auto filename=maildir_find(INBOX "." DRAFTS, c);

	if (filename.empty())	return (-1);

	if (search_spell(filename.c_str(), 0, 0) == 0)
		return (-1);

	return (0);
}

/*
** Search for misspelled words.
*/

static const rfc2045::entity *findtext(const rfc2045::entity &);

static std::string spell_check(const std::string &, unsigned, unsigned,
			       std::string_view,
			       std::string_view,
			       std::string_view, bool &);

static bool search_spell(const char *filename, unsigned parnum, unsigned pos)
{
	std::string newtext, current_line;
	std::string new_line;
	std::string_view ignoreword, replacefrom, replaceto;
	bool	checked=false;

	rfc822::fdstreambuf fp{maildir_safeopen(filename, O_RDONLY, 0)};

	if (fp.error())
		return false;

	rfc2045::entity message;

	{
		std::istreambuf_iterator<char> b{&fp}, e;

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		message.parse(parser);
	}

	auto textp=findtext(message);

	if (!textp)
	{
		return false;
	}

	fp.pubseekpos(textp->startbody);

	bool	made_replacements=false, has_misspelling=false;
	unsigned paragraph=0;
        for (auto start_body=textp->startbody ;
	     start_body < textp->endbody; start_body++)
	{
		auto c=fp.sbumpc();

		if (c == std::char_traits<char>::eof())
			break;

		if (c != '\n')
		{
			current_line += c;
			continue;
		}

		if (current_line == "-- ") // Stop at sig line.
			break;

		if (parnum)
		{
			--parnum;
			newtext += current_line;
			newtext += '\n';
			current_line.clear();
			++paragraph;
			continue;
		}

		if (!checked)
		{
			checked=true;

			std::string_view word{cgi("word")};

			auto l=word.size();
			if (l > 0)
			{

/* Ok, what should we do? */

				std::string_view newword=cgi("REPLACE");

				if (newword.empty() || newword == "#other")
					newword=cgi("OTHER");
				/*
				** Perhaps they entered the word without
				** checking this checkmark.
				*/
				else if (newword.substr(0, 1) == "#")
					newword="";

				if (newword.size() &&
				    pos + l <= current_line.size())
				{
					std::string tempbuf;

					tempbuf.reserve(current_line.size()-l
							+ newword.size());

					tempbuf=std::string_view{current_line}
						.substr(0, pos);
					tempbuf += newword;
					tempbuf += std::string_view{
						current_line
							}.substr(pos+l);

					pos += newword.size();

					if (*cgi("REPLACEALL"))
					{
						replacefrom=cgi("word");
						replaceto=newword;
					}
					current_line=tempbuf;
					made_replacements=true;
				}
				else
				{
					pos += l;
					std::string_view replace{
						cgi("REPLACE")
					};

					if (replace == "#ignoreall")
						ignoreword=cgi("word");
				}

				if (strcmp(cgi("REPLACE"),
						"#insert") == 0)
				{
					spelladd(cgi("word"));
				}
			}
		}

		if (std::string_view{current_line}.substr(0, 1) == ">")
		{
			newtext += current_line;
			newtext += "\n";
			pos=0;
			current_line.clear();
			++paragraph;
			continue;
		}
		if (!has_misspelling)
		{
			new_line=spell_check(current_line, paragraph, pos,
				ignoreword, replacefrom, replaceto,
				has_misspelling);
			if (!new_line.empty())
			{
				newtext += new_line;
				made_replacements=true;
			}
			else	newtext += current_line;
		}
		else	newtext += current_line;
		newtext += "\n";
		pos=0;
		current_line.clear();
		++paragraph;
	}
	if (!current_line.empty())
		newtext += "\n";

	if (made_replacements)
	{
		newmsg_createdraft_do(filename, newtext.c_str(),
				      NEWMSG_SQISPELL);

		if (*cgi("error"))
		{
			has_misspelling=false;	/* Abort spell checking */
		}
	}

	if (ignoreword.size())
	{
		static std::string globignore;

		globignore.clear();
		globignore.reserve(std::string_view{cgi("globignore")}.size()
				   + 2 + ignoreword.size());

		globignore=cgi("globignore");

		if (globignore.size())
			globignore += ":";
		globignore += ignoreword;
		cgi_put("globignore", globignore);
	}

	if (replacefrom.size())
	{
		static std::string globreplace;

		globreplace.clear();

		globreplace.reserve(
			std::string_view{cgi("globreplace")}.size()+3
			+ replacefrom.size() + replaceto.size());

		globreplace=cgi("globreplace");
		if (globreplace.size())
			globreplace += ":";

		globreplace += replacefrom;
		globreplace += ":";
		globreplace += replaceto;
		cgi_put("globreplace", globreplace);
	}

	return has_misspelling;
}

static const rfc2045::entity *findtext(const rfc2045::entity &rfcp)
{
	if (std::string_view{rfcp.content_type.value}.substr(0, 5) == "text/")
		return &rfcp;

	for (auto &subentity:rfcp.subentities)
	{
		auto ptr=findtext(subentity);

		if (ptr)
			return ptr;
	}
	return nullptr;
}

/*
** Ok, check a single paragraph, starting at position #pos.
**
** If some replacements were made due to previous saved 'replace all' words,
** return the text of the modified line.  Otherwise return NULL.
**
** Set *hasmisspelled to 1 if there are some misspellings in this line.
*/

static std::string ispellline;
static std::optional<ispell> ispellptr;
static unsigned paragraph;

static bool spellignore(std::string_view);
static std::string spellreplace(std::string_view);

static std::string spell_check(const std::string &line, unsigned pnum,
			       unsigned pos,
			       std::string_view ignoreword,
			       std::string_view replacefrom,
			       std::string_view replaceto,
			       bool &hasmisspelled)
{
	std::string newline=line;
	std::string_view newword;

	if (line.size() <= pos)	return {};	/* Sanity check */

	ispellptr.reset();
	ispellptr.emplace(sqwebmail_content_ispelldict.c_str(), line.substr(pos));
	if (!ispellptr)	enomem();
	for (auto &word:ispellptr->misspelled_words)
		word.word_pos += pos;

	ssize_t adjust=0;
	for (auto &msp:ispellptr->misspelled_words)
	{
		msp.word_pos += adjust;

		if (msp.word_pos >= newline.size() ||
		    (newline.size()-msp.word_pos) < msp.misspelled_word.size())
			continue; // Sanity check

		if ((!ignoreword.empty() &&
		     msp.misspelled_word == ignoreword)
		    || spellignore(msp.misspelled_word))
		{
			msp.misspelled_word="";
			continue;
		}

		newword="";
		if ( !replacefrom.empty() &&
		     msp.misspelled_word == replacefrom)
			newword=replaceto;

		std::string replword;

		if (!newword.empty() ||
		    !(newword=replword=spellreplace(
			      msp.misspelled_word
		      )).empty())
		{
			std::string p;

			p.reserve(newline.size()+newword.size());

			p=newline.substr(0, msp.word_pos);
			p += newword;
			p += newline.substr(msp.word_pos+
					    msp.misspelled_word.size());

			newline=std::move(p);
			adjust += newword.size()-msp.misspelled_word.size();
			msp.misspelled_word="";
			continue;
		}
		hasmisspelled=true;
		paragraph=pnum;
		break;
	}
	if (!hasmisspelled)
	{
		ispellptr.reset();
	}
	else
	{
		ispellline=newline;
	}
	return (newline);
}

void spell_show()
{
const char *draftmessage=cgi("draftmessage");
 size_t	p, l=ispellline.size(), n;
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

	auto msp=ispellptr->misspelled_words.begin();
	while (msp != ispellptr->misspelled_words.end())
	{
		if (!msp->misspelled_word.empty())	break;
		++msp;
	}
	if (msp==ispellptr->misspelled_words.end())	enomem();

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
		while (p && ispellline[p-1] != ' ')
			--p;

		for (n=p; n<(size_t)(msp->word_pos); n++)
			if (ispellline[n] == ' ')
			{
				while (n < p && ispellline[n] == ' ')
					++n;
				if (n < msp->word_pos)
					p=n;
				break;
			}
		printf("...&nbsp;");
	}
	else
		p=0;

	output_attrencoded(
		std::string_view{ispellline}.substr(p, msp->word_pos-p)
	);
	printf("<strong>");
	output_attrencoded(
		std::string_view{ispellline}.substr(msp->word_pos,
						    msp->misspelled_word.size()
		)
	);
	printf("</strong>");

	p=msp->word_pos+msp->misspelled_word.size();
	if (l-p < 30)
	{
		n=l-p; // show the rest of the line
	}
	else
	{
		n=30;

		// find where this word ends.

		while (n+p < l)
		{
			if (ispellline[n+p] == ' ')
				break;
			++n;
		}

		// that's the opening bid, now try to get under the limit
		// by finding where this word starts.
		for (size_t i=n; i > 0; --i)
		{
			if (ispellline[i+p-1] == ' ')
			{
				while (i > 0)
				{
					if (ispellline[i+p-1] != ' ')
					{
						n=i;
						break;
					}
					--i;
				}
				break;
			}
		}
	}

	output_attrencoded(std::string_view{ispellline}.substr(p, n));

	if (n != l-p)
		printf("&nbsp;...");
	printf("</span></td></tr></table><br />");
	printf("<table border=\"1\" cellpadding=\"8\" class=\"spellcheck-main\"><tr><td>");

	printf("<table border=\"0\">");

	for (auto &suggestion:msp->suggestions)
	{
		printf("<tr><td>%s</td><td><input type=\"radio\" name=\"REPLACE\" value=\"",
		       replacelab);

		fwrite(suggestion.data(), suggestion.size(), 1, stdout);
		printf("\" /></td><td>");
		fwrite(suggestion.data(), suggestion.size(), 1, stdout);
		printf("</td></tr>\n");
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

static std::string dictname()
{
	std::string p;

	if (!sqwebmail_content_ispelldict.empty())
	{
		p.reserve(sqwebmail_content_ispelldict.size()+20);

		p="sqwebmail-dict-";
		p+=sqwebmail_content_ispelldict;
	}
	else
	{
		p="sqwebmail-dict";
	}

	return p;
}

static bool spellignore(std::string_view word)
{
	std::ifstream i{dictname()};

	for (std::string buf; std::getline(i, buf); )
	{
		if (word == buf)
		{
			return true;
		}
	}
	i.close();

	std::string_view c=cgi("globignore");

	while (!c.empty())
	{
		size_t i=c.find(':');

		if (i > c.size())
			i=c.size();

		std::string_view q=c.substr(0, i);

		if (i < c.size())
			++i;
		c=c.substr(i);

		if (q == word)
		{
			return (true);
		}
	}

	return (false);
}

static void spelladd(const char *word)
{
	std::ofstream o{dictname(),
			std::ios_base::out | std::ios_base::app};

	o << word << "\n";
}

static std::string spellreplace(std::string_view word)
{
	std::string p;
	char *r;
	const char *c=cgi("globreplace");

	p.reserve(strlen(c));
	p=c;

	for (auto q=p.data();
	     (q=strtok(q, ":")) != 0 && (r=strtok(0, ":")) != 0; q=0)
	{
		if (q == word)
		{
			return r;
		}
	}
	return "";
}

void spell_check_continue()
{
const char *filename=cgi("draftmessage");
unsigned parnum=atol(cgi("row"));
unsigned pos=atol(cgi("col"));

	CHECKFILENAME(filename);
	auto draftfilename=maildir_find(INBOX "." DRAFTS, filename);
	if (draftfilename.empty())
	{
		output_form("folder.html");
		return;
	}

	if (search_spell(draftfilename.c_str(), parnum, pos) &&
		*cgi("continue"))
		output_form("spellchk.html");
	else
	{
		cgi_put("draft", cgi("draftmessage"));
		cgi_put("previewmsg","SPELLCHK");
		output_form("newmsg.html");
	}
}

void ispell_cleanup()
{
	ispellline.clear();
}
