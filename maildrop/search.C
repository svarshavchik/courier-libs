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
	if (match_data)
	{
		pcre2_match_data_free(match_data);
		match_data=NULL;
	}

	if (pcre_regexp)
	{
		pcre2_code_free(pcre_regexp);
		pcre_regexp=NULL;
	}
}

int	Search::init(const char *expr, const char *opts)
{
	match_top_header=0;
	match_other_headers=0;
	match_body=0;
	weight1=1;
	weight2=1;
	scoring_match=0;
	score=0;

	if (strchr(opts, 'h'))	match_top_header=match_other_headers=1;
	if (strchr(opts, 'H'))	match_top_header=1;

	if (strchr(opts, 'b'))	match_body=1;
	if (!match_top_header && !match_other_headers && !match_body)
	{
		match_top_header=1;
		match_other_headers=1;
		if (strchr(opts, 'w'))	match_body=1;
	}

	int errcode;

	cleanup();

	PCRE2_SIZE errindex;

	pcre_regexp=pcre2_compile((PCRE2_SPTR8)expr,
				  PCRE2_ZERO_TERMINATED,
				  PCRE2_UTF | (strchr(opts, 'D') ? 0:PCRE2_CASELESS),
				  &errcode,
				  &errindex,
				  NULL);

	if (!pcre_regexp)
	{
		Buffer b;

		PCRE2_UCHAR buffer[256];
		pcre2_get_error_message(errcode, buffer, sizeof(buffer));

		b="Invalid regular expression, offset ";
		add_integer(b, errindex);
		b += " of: ";
		b += expr;
		b += ": ";
		b += (char *)buffer;
		b += "\n";
		b.push_back_0();
		merr.write(b.c_str());
		return -1;
	}

	match_data= pcre2_match_data_create_from_pattern(
		pcre_regexp, NULL
	);

	if (!match_data)
	{
		Buffer b;

		b="Failed to create match data for: ";
		b += expr;
		b += "\n";
		b.push_back_0();
		merr.write(b.c_str());
		cleanup();
		return -1;
	}
	search_expr=expr;

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
		msg += expr;
		msg += "/ against ";
		msg += str;
		msg += "\n";
		msg.push_back_0();
		merr.write(msg.c_str());
	}

	int startoffset=0;
	const char *orig_str=str;

	for (;;)
	{
		int rc=pcre2_match(pcre_regexp,
				   (PCRE2_SPTR8)orig_str,
				   strlen(orig_str),
				   startoffset,
				   0,
				   match_data,
				   NULL);

		if (rc < 0 )
			break;


		PCRE2_SIZE *ovector=pcre2_get_ovector_pointer(match_data);
		uint32_t ovector_count=pcre2_get_ovector_count(match_data);

		score += weight1;
		weight1 *= weight2;

		if (!scoring_match || foreachp)
		{
			init_match_vars(orig_str, ovector, ovector_count,
					foreachp);
			if (!foreachp)
				break;
		}

		if (!ovector || ovector_count <= 0)
			break;

		startoffset=ovector[1];

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

	if (!match_top_header && !match_other_headers)
		decode_cb.flags |= RFC2045_DECODEMSG_NOHEADERS;
	else if (match_top_header && !match_other_headers)
		decode_cb.flags |= RFC2045_DECODEMSG_NOATTACHHEADERS;

	if (!match_body)
		decode_cb.flags |= RFC2045_DECODEMSG_NOBODY;

	current_line.clear();
	decode_cb.output_func=&Search::search_cb;
	decode_cb.arg=this;
	foreachp_arg=foreachp;
	rfc2045_decodemsgtoutf8(&msg.rfc2045src_parser,
				msg.rfc2045p, &decode_cb);
	if (current_line.size() >= 1)
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
			current_line.push_back_0();

			if (VerboseLevel() > 2)
			{
			Buffer	msg;

				msg="Matching /";

				msg += search_expr;

				msg += "/ against ";
				msg += current_line.c_str();
				msg += "\n";
				msg.push_back_0();
				merr.write(msg.c_str());
			}

			const char *orig_str=current_line.c_str();

			int rc=pcre2_match(pcre_regexp,
					   (PCRE2_SPTR8)orig_str,
					   strlen(orig_str),
					   0,
					   0,
					   match_data,
					   NULL);

			if (rc >= 0)
			{
				score += weight1;
				weight1 *= weight2;

				if (!scoring_match || foreachp_arg)
				{
					PCRE2_SIZE *ovector=
						pcre2_get_ovector_pointer(
							match_data);
					uint32_t ovector_count=
						pcre2_get_ovector_count(
							match_data);

					init_match_vars(orig_str,
							ovector,
							ovector_count,
							foreachp_arg);
					if (!foreachp_arg)
						// Stop searching now
						return (-1);
				}
			}
			else	if (VerboseLevel() > 2)
				merr.write("Not matched.\n");

			current_line.clear();

			++ptr;
			--cnt;
			continue;
		}


		for (i=0; i<cnt; ++i)
			if (ptr[i] == '\n')
				break;
		current_line.append(ptr, ptr+i);
		ptr += i;
		cnt -= i;
	}
	return (0);
}

void Search::init_match_vars(const char *str,
			     PCRE2_SIZE *offsets,
			     uint32_t nranges,
			     Buffer *foreachp)
{
	Buffer varname;
	uint32_t cnt;

	if (!offsets)
		return;

	for (cnt=0; cnt<nranges; cnt++)
	{
		varname="MATCH";
		if (cnt)
			add_integer(varname, cnt);


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
			(*foreachp).push_back_0();
		}

		SetVar(varname, v);
	}
}
