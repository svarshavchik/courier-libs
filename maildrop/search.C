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

	const char *errptr;

	cleanup();

	int errindex;

	pcre_regexp=pcre_compile(expr,
				 PCRE_UTF8 | (strchr(opts, 'D') ? 0:PCRE_CASELESS),
				 &errptr,
				 &errindex, 0);

	if (!pcre_regexp)
	{
		Buffer b;

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

	pcre_regexp_extra=pcre_study(pcre_regexp, 0, &errptr);

	if (errptr)
	{
		Buffer b;

		b="Error parsing regular expression: ";
		b += expr;
		b += ": ";
		b += errptr;
		b += "\n";
		b += '\0';
		merr.write(b);
		return -1;
	}

	search_expr=expr;
	int cnt=0;

	pcre_fullinfo(pcre_regexp, pcre_regexp_extra,
		      PCRE_INFO_CAPTURECOUNT, &cnt);

	pcre_vector_count=(cnt+1)*3;

	pcre_vectors=(int *)malloc(pcre_vector_count*sizeof(int));

	if (!pcre_vectors)
	{
		Buffer b;

		b=strerror(errno);
		b += "\n";
		b += '\0';
		merr.write(b);
		return -1;
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
	return (findinline(msg, expr, foreachp));
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
	struct rfc2045_decodemsgtoutf8_cb decode_cb;

	memset(&decode_cb, 0, sizeof(decode_cb));

	if (!match_header)
		decode_cb.flags |= RFC2045_DECODEMSG_NOHEADERS;

	if (!match_body)
		decode_cb.flags |= RFC2045_DECODEMSG_NOBODY;

	current_line.reset();
	decode_cb.output_func=&Search::search_cb;
	decode_cb.arg=this;
	foreachp_arg=foreachp;
	rfc2045_decodemsgtoutf8(&msg.rfc2045src_parser,
				msg.rfc2045p, &decode_cb);
	if (current_line.Length() >= 1)
		search_cb("\n", 1);
	return 0;
}

int Search::search_cb(const char *ptr, size_t cnt, void *arg)
{
	return ((Search *)arg)->search_cb(ptr, cnt);
}

int Search::search_cb(const char *ptr, size_t cnt)
{
	while (cnt)
	{
		size_t i;

		if (*ptr == '\n')
		{
			current_line += '\0';

			if (VerboseLevel() > 2)
			{
			Buffer	msg;

				msg="Matching /";

				{
					Buffer cpy;

					cpy += search_expr;
					cpy += '\0';
					msg.append(cpy);
				}
				msg.append("/ against ");
				msg += current_line;
				msg.pop();	// Trailing null byte.
				msg += '\n';
				msg += '\0';
				merr.write(msg);
			}

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

				if (!scoring_match || foreachp_arg)
				{
					init_match_vars(orig_str,
							match_count,
							pcre_vectors,
							foreachp_arg);
					if (!foreachp_arg)
						// Stop searching now
						return (-1);
				}
			}
			else	if (VerboseLevel() > 2)
				merr.write("Not matched.\n");

			current_line.reset();

			++ptr;
			--cnt;
			continue;
		}


		for (i=0; i<cnt; ++i)
			if (ptr[i] == '\n')
				break;
		current_line.append(ptr, i);
		ptr += i;
		cnt -= i;
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

		if (cnt == 0 && foreachp)
		{
			*foreachp += v;
			*foreachp += '\0';
		}

		SetVar(varname, v);
	}
}
