#include "config.h"
#include	"search.h"
#include	"message.h"
#include	"messageinfo.h"
#include	"rematchstr.h"
#include	"rematchmsg.h"
#include	"varlist.h"
#include	<string.h>
#include	<ctype.h>
#include	<stdlib.h>


void Search::cleanup()
{
	if (pcre_regexp_extra)
	{
		pcre_free(pcre_regexp_extra);
		pcre_regexp_extra=NULL;
	}
	if (pcre_regexp)
	{
		pcre_free(pcre_regexp);
		pcre_regexp=NULL;
	}

	if (pcre_vectors)
	{
		free(pcre_vectors);
		pcre_vectors=NULL;
	}
}

int	Search::init(const char *expr, const char *opts)
{
	int	dummy;

	match_header=0;
	match_body=0;
	weight1=1;
	weight2=1;
	scoring_match=0;
	score=0;

	if (strchr(opts, 'h'))	match_header=1;
	if (strchr(opts, 'b'))	match_body=1;
	if (!match_header && !match_body)
	{
		match_header=1;
		if (strchr(opts, 'w'))	match_body=1;
	}

	Buffer b;

	b="MAILDROP_OLD_REGEXP";

	const char *p=GetVarStr(b);

	if (atoi(p ? p:"0") == 0)
	{
		const char *errptr;

		cleanup();

		if (strchr(opts, 'w'))
		{
			b="Pattern option 'w' is valid only when MAILDROP_OLD_REGEXP is set\n";
			b += '\0';
			merr.write(b);
			return -1;
		}

		int errindex;

		pcre_regexp=pcre_compile(expr,
					 strchr(opts, 'D') ? 0:PCRE_CASELESS,
					 &errptr,
					 &errindex, 0);

		if (!pcre_regexp)
		{
			b="Invalid regular expression, offset ";
			b.append((unsigned long)errindex);
			b += " of: ";
			b += expr;
			b += ": ";
			b += errptr;
			b += "\n";
			b += '\0';
			merr.write(b);
			return -1;
		}

		pcre_regexp_extra=pcre_study(pcre_regexp, 0,
					     &errptr);

		if (errptr)
		{
			b="Error parsing regular expression: ";
			b += expr;
			b += ": ";
			b += errptr;
			b += "\n";
			b += '\0';
			merr.write(b);
			return -1;
		}

		int cnt=0;

		pcre_fullinfo(pcre_regexp, pcre_regexp_extra,
			      PCRE_INFO_CAPTURECOUNT, &cnt);

		pcre_vector_count=(cnt+1)*3;

		pcre_vectors=(int *)malloc(pcre_vector_count*sizeof(int));

		if (!pcre_vectors)
		{
			b=strerror(errno);
			b += "\n";
			b += '\0';
			merr.write(b);
			return -1;
		}
	}				
	else
	{
		if (regexp.Compile(expr, strchr(opts, 'D') ? 1:0, dummy))
			return (-1);
	}

	while (*opts)
	{
		if (*opts == '.' || isdigit(*opts) || *opts == '-' ||
			*opts == '+')
		{
			weight1=atof(opts);
			while (*opts && *opts != ',')	++opts;
			if (*opts == ',')
			{
				++opts;
				if (*opts == '.' || isdigit(*opts) ||
					*opts == '-' || *opts == '+')
					weight2=atof(opts);
			}
			scoring_match=1;
			break;
		}
		++opts;
	}
	return (0);
}

int Search::find(Message &msg, MessageInfo &,
	const char *expr, const char *opts, Buffer *foreachp)
{
	if (init(expr, opts))	return (-1);

	msg.Rewind();
	return (strchr(opts, 'w') ? findinsection(msg, expr, foreachp):
		findinline(msg, expr, foreachp));
}

int Search::find(const char *str, const char *expr, const char *opts,
		Buffer *foreachp)
{
	if (init(expr, opts))	return (-1);

	if (VerboseLevel() > 2)
	{
	Buffer	msg;

		msg="Matching /";
		msg.append(expr);
		msg.append("/ against ");
		msg += str;
		msg += '\n';
		msg += '\0';
		merr.write(msg);
	}

	int startoffset=0;
	const char *orig_str=str;
	int match_count=0;

	for (;;)
	{
		if (pcre_regexp)
		{
			match_count=pcre_exec(pcre_regexp, pcre_regexp_extra,
					      orig_str, strlen(orig_str),
					      startoffset,
					      0,
					      pcre_vectors,
					      pcre_vector_count);
			if (match_count <= 0)
				break;
			startoffset=pcre_vectors[1];

			score += weight1;
			weight1 *= weight2;

			if (!scoring_match || foreachp)
			{
				init_match_vars(orig_str, match_count,
						pcre_vectors, foreachp);
				if (!foreachp)
					break;
			}
			continue;
		}

		ReMatchStr match(str);

		if ( regexp.Match(match))	break;

		score += weight1;
		weight1 *= weight2;

		if (!scoring_match || foreachp)
		{
			match.SetCurrentPos(0);
			init_match_vars(match, foreachp);
			if (!foreachp)
				break;	// No need for more.
		}

	Re *p;
	off_t	c=0;

		for (p= &regexp; p; )
			c += p->MatchCount( &p );
		if (c == 0)
		{
			if (!*str)	break;
			++c;
		}
		str += c;
	}
	return (0);
}

//////////////////////////////////////////////////////////////////////////////
//
// Search individual lines for the pattern (transparently concatenate
// continued headers.
//
//////////////////////////////////////////////////////////////////////////////

int Search::findinline(Message &msg, const char *expr, Buffer *foreachp)
{
	current_line.reset();
	if (msg.appendline(current_line))	return (0);	// Empty msg

int	eof;

	for (;;)
	{
	int	c='\n';

		next_line.reset();
		if ((eof=msg.appendline(next_line)) == 0)
		{
			c=(unsigned char)*(const char *)next_line;

			if ( isspace( c ) && c != '\n')
				// Continued header
			{
				current_line.pop();
				current_line += next_line;
				continue;
			}
		}
		current_line.pop();

		current_line += '\0';

		if (match_header)
		{
			if (VerboseLevel() > 2)
			{
			Buffer	msg;

				msg="Matching /";
				msg.append(expr);
				msg.append("/ against ");
				msg += current_line;
				msg.pop();	// Trailing null byte.
				msg += '\n';
				msg += '\0';
				merr.write(msg);
			}

			if (pcre_regexp)
			{
				const char *orig_str=current_line;
				int match_count;

				match_count=pcre_exec(pcre_regexp,
						      pcre_regexp_extra,
						      orig_str,
						      strlen(orig_str),
						      0,
						      0,
						      pcre_vectors,
						      pcre_vector_count);

				if (match_count > 0)
				{
					score += weight1;
					weight1 *= weight2;

					if (!scoring_match || foreachp)
					{
						init_match_vars(orig_str,
								match_count,
								pcre_vectors,
								foreachp);
						if (!foreachp)
							return (0);
					}
				}
				else	if (VerboseLevel() > 2)
					merr.write("Not matched.\n");
			}
			else
			{
				ReMatchStr match(current_line);

				if (regexp.Match(match) == 0)
				{
					score += weight1;
					weight1 *= weight2;
					if (!scoring_match || foreachp)
					{
						match.SetCurrentPos(0);
						init_match_vars(match,
								foreachp);
						if (!foreachp)
							return (0);
					}
				}
				else	if (VerboseLevel() > 2)
					merr.write("Not matched.\n");
			}
		}
		if ( c == '\n')	break;
		current_line=next_line;
	}
	if (!match_body || eof)	return (0);

	while (current_line.reset(), msg.appendline(current_line) == 0)
	{
		current_line.pop();
		current_line += '\0';

		if (VerboseLevel() > 2)
		{
		Buffer	msg;

			msg="Matching /";
			msg.append(expr);
			msg.append("/ against ");
			msg += current_line;
			msg.pop();	// Trailing null byte.
			msg += '\n';
			msg += '\0';
			merr.write(msg);
		}

		if (pcre_regexp)
		{
			const char *orig_str=current_line;
			int match_count;

			match_count=pcre_exec(pcre_regexp,
					      pcre_regexp_extra,
					      orig_str,
					      strlen(orig_str),
					      0,
					      0,
					      pcre_vectors,
					      pcre_vector_count);

			if (match_count > 0)
			{
				score += weight1;
				weight1 *= weight2;

				if (!scoring_match || foreachp)
				{
					init_match_vars(orig_str,
							match_count,
							pcre_vectors,
							foreachp);
					if (!foreachp)
						return (0);
				}
			}
			else	if (VerboseLevel() > 2)
				merr.write("Not matched.\n");

			continue;
		}

		ReMatchStr match(current_line);

		if (regexp.Match(match) == 0)
		{
			score += weight1;
			weight1 *= weight2;
			if (!scoring_match || foreachp)
			{
				match.SetCurrentPos(0);
				init_match_vars(match, foreachp);
				if (!foreachp)
					return (0);
			}
		}
		else	if (VerboseLevel() > 2)
				merr.write("Not matched.\n");
	}
	return (0);
}

///////////////////////////////////////////////////////////////////////////
//
// Search anchored in the entire message.
//
///////////////////////////////////////////////////////////////////////////

int Search::findinsection(Message &msg, const char *expr, Buffer *foreachp)
{
	if (!match_header && !match_body)	return (0);	// Huh?

	if (VerboseLevel() > 2)
	{
	Buffer	m;

		m="Matching /";
		m.append(expr);
		m.append("/ against");
		if (match_header)
			m.append(" header");
		if (match_body)
			m.append(" body");
		m += '\n';
		m += '\0';
		merr.write(m);
	}

	if (!match_header)
	{
	Buffer	dummy;

		do
		{
			dummy.reset();
			if (msg.appendline(dummy) < 0)	return (0);
						// No message body, give up.
		} while (dummy.Length() != 1 ||
				*(const char *)dummy != '\n');
	}

off_t start_pos=msg.tell();
ReMatchMsg	match_msg(&msg, !match_body, match_header);

	while ( match_msg.CurrentChar() >= 0 && regexp.Match(match_msg) == 0)
	{
		score += weight1;
		weight1 *= weight2;

		if (!scoring_match || foreachp)
		{
			match_msg.SetCurrentPos(start_pos);
			init_match_vars(match_msg, foreachp);
			if (!foreachp)
				break;	// No need for more.
		}

	Re *p;
	off_t c=0;

		for (p= &regexp; p; )
			c += p->MatchCount( &p );
		if (c == 0)	++c;
		start_pos += c;
		match_msg.SetCurrentPos(start_pos);
	}
	return (0);
}

void Search::init_match_vars(const char *str, int nranges, int *offsets,
			     Buffer *foreachp)
{
	Buffer varname;
	int cnt;

	for (cnt=0; cnt<nranges; cnt++)
	{
		varname="MATCH";
		if (cnt)
			varname.append((unsigned long)cnt);


		Buffer v;

		int i, j;

		i=offsets[cnt*2];
		j=offsets[cnt*2+1];

		if (i < j)
		{
			size_t s=j-i;

			char *ptr=(char *)malloc(s+1);
			if (ptr)
			{
				memcpy(ptr, str + i, s);
				ptr[s]=0;
				v=ptr;
				free(ptr);
			}
		}

		if (foreachp)
		{
			*foreachp += v;
			*foreachp += '\0';
		}

		SetVar(varname, v);
	}
}

void Search::init_match_vars(ReMatch &m, Buffer *foreachp)
{
Re	*p;
Buffer	buf;
Buffer	varname;
unsigned long varnamecount=1;

	varname="MATCH";
	for (p= &regexp; p; )
	{
	Re	*q=p;
	unsigned	count=p->MatchCount(&p);

		buf.reset();
		while (count)
		{
			buf.push( m.NextChar() );
			count--;
		}

		if ( !q->IsDummy())
		{
			if (foreachp)
			{
				*foreachp += buf;
				*foreachp += '\0';
			}
			else
			{
				SetVar(varname, buf);
				++varnamecount;
				varname="MATCH";
				varname.append(varnamecount);
			}
		}
	}
}
