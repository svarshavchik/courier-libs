/*
** Copyright 2011 Double Precision, Inc.  See COPYING for
** distribution information.
*/

/*
*/

#include "html.h"

#include <courier-unicode.h>
#include "rfc2045/rfc2045.h"
#include <stdlib.h>
#include <string.h>

#define SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

struct taginfo {

	char tagname[16];
	int flags;
};

static const char hex[]="0123456789ABCDEF";


#define FLAG_NOENDTAG	0x01
#define FLAG_DISCARD	0x02
#define FLAG_NOPRINT	0x04

#define FLAG_BLOCKQUOTE_CITE 0x1000

static const struct taginfo tags[]={
	{"a"},
	{"abbr"},
	{"acronym"},
	{"address"},
	{"b"},
	{"base",	FLAG_NOENDTAG},
	{"basefont",	FLAG_NOENDTAG},
	{"bdo"},
	{"big"},
	{"blockquote"},
	{"br",	FLAG_NOENDTAG},
	{"caption"},
	{"center"},
	{"cite"},
	{"code"},
	{"col",		FLAG_NOENDTAG},
	{"colgroup"},
	{"dd"},
	{"del"},
	{"dfn"},
	{"dir"},
	{"div"},
	{"dl"},
	{"dt"},
	{"em"},
	{"font"},
	{"h1"},
	{"h2"},
	{"h3"},
	{"h4"},
	{"h5"},
	{"h6"},
	{"hr",		FLAG_NOENDTAG},
	{"i"},
	{"img",		FLAG_NOENDTAG},
	{"ins"},
	{"kbd"},
	{"li"},
	{"menu"},
	{"ol"},
	{"p"},
	{"pre"},
	{"q"},
	{"s"},
	{"samp"},
	{"script",	FLAG_DISCARD},
	{"small"},
	{"span"},
	{"strike"},
	{"strong"},
	{"style",	FLAG_DISCARD},
	{"sub"},
	{"sup"},
	{"table"},
	{"tbody"},
	{"td"},
	{"tfoot"},
	{"th"},
	{"thead"},
	{"title"},
	{"tr"},
	{"tt"},
	{"u"},
	{"ul"},
	{"var"},
};

static const struct taginfo div_tag={"div"};

static const struct taginfo blockquote_cite_tag={"blockquote",
						 FLAG_BLOCKQUOTE_CITE};

static const struct taginfo unknown_tag={" unknown", FLAG_NOPRINT};

static const struct taginfo span_discard_tag={" discard",
					      FLAG_DISCARD | FLAG_NOPRINT};

struct attr {
	struct unicode_buf name; /* Attribute name */
	struct unicode_buf value; /* Attribute value */
};

struct htmlfilter_info {

	/* The output function receives the HTML-filtered stream */

	void (*output_func)(const char32_t *, size_t, void *);
	void *output_func_arg;

	/* Content base for relative URLs */
	char *contentbase;

	/* Prepend to http: and https: links */
	char *http_prefix;

	/* Prepent to mailto: links */
	char *mailto_prefix;

	/* A cid: link gets passed to this function for processing. */
	char *(*convert_cid_func)(const char *, void *);
	void *convert_cid_func_arg;

	/* Current handle for the input HTML stream */

	size_t (*handler_func)(struct htmlfilter_info *,
			       const char32_t *,
			       size_t);

	/*
	** An & entity name. Or a tag name. Or an attribute name or value.
	*/

	struct unicode_buf atom;

	/*
	** An attribute value
	*/

	struct unicode_buf value;

	/*
	** Another atom
	*/

	struct unicode_buf atom2;

	/*
	** Quoting character
	*/

	char32_t value_quote;

	/* Current tag being processed */
	const struct taginfo *tag;

	/* Whether parsed an empty tag */
	int tag_empty;

	struct attr attrs[32];
	size_t attrs_index;

	/*
	** Current list of active elements.
	** We limit the number of open elements to 128
	*/

	const struct taginfo *open_elements[128];
	size_t n_open_elements;

	/*
	** How many elements have been open since the first element whose
	** contents should be discarded
	*/

	size_t n_discarded;
};

static void free_last_attr(struct htmlfilter_info *p)
{
	size_t i=--p->attrs_index;

	unicode_buf_deinit(&p->attrs[i].name);
	unicode_buf_deinit(&p->attrs[i].value);
}

static void free_attrs(struct htmlfilter_info *p)
{
	while (p->attrs_index)
		free_last_attr(p);
}

static size_t handle_chars(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt);

static size_t handle_entity(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt);

static size_t seen_lt(struct htmlfilter_info *p,
		      const char32_t *uc,
		      size_t cnt);

static size_t seen_ltexcl(struct htmlfilter_info *p,
			  const char32_t *uc,
			  size_t cnt);

static size_t seen_sgentity(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt);

static size_t seen_ltspace(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt);

static size_t seen_pi(struct htmlfilter_info *p,
		      const char32_t *uc,
		      size_t cnt);

static size_t seen_piq(struct htmlfilter_info *p,
		       const char32_t *uc,
		       size_t cnt);

static size_t seen_comment(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt);

static size_t seen_commentdash(struct htmlfilter_info *p,
			       const char32_t *uc,
			       size_t cnt);

static size_t seen_commentdashdash(struct htmlfilter_info *p,
				   const char32_t *uc,
				   size_t cnt);

static size_t seen_closing_elem(struct htmlfilter_info *p,
				const char32_t *uc,
				size_t cnt);

static size_t seen_opening_elem(struct htmlfilter_info *p,
				const char32_t *uc,
				size_t cnt);

static size_t seen_attr(struct htmlfilter_info *p,
			const char32_t *uc,
			size_t cnt);

static size_t seen_attrname(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt);

static size_t seen_attrvalue_1stchar(struct htmlfilter_info *p,
				     const char32_t *uc,
				     size_t cnt);

static size_t seen_attrvalue(struct htmlfilter_info *p,
			     const char32_t *uc,
			     size_t cnt);

static size_t seen_attrvalue_entity(struct htmlfilter_info *p,
				    const char32_t *uc,
				    size_t cnt);

struct htmlfilter_info *htmlfilter_alloc(void (*output_func)
					 (const char32_t *, size_t, void *),
					 void *output_func_arg)
{
	struct htmlfilter_info *p;

	p=calloc(1, sizeof(*p));
	if (!p)
		return p;

	p->output_func=output_func;
	p->output_func_arg=output_func_arg;

	unicode_buf_init(&p->atom, 2048);
	unicode_buf_init(&p->atom2, 2048);
	unicode_buf_init(&p->value, 8192);

	p->handler_func=handle_chars;
	return p;
}

static void close_elements_until(struct htmlfilter_info *p, size_t i);

void htmlfilter_free(struct htmlfilter_info *p)
{
	close_elements_until(p, 0);

	free_attrs(p);

	unicode_buf_deinit(&p->atom);
	unicode_buf_deinit(&p->atom2);
	unicode_buf_deinit(&p->value);

	if (p->contentbase)
		free(p->contentbase);

	if (p->http_prefix)
		free(p->http_prefix);

	if (p->mailto_prefix)
		free(p->mailto_prefix);

	free(p);
}

void htmlfilter_set_contentbase(struct htmlfilter_info *p,
				const char *contentbase)
{
	if (p->contentbase)
		free(p->contentbase);

	p->contentbase=strdup(contentbase);
}


void htmlfilter_set_http_prefix(struct htmlfilter_info *p,
				const char *http_prefix)
{
	if (p->http_prefix)
		free(p->http_prefix);

	p->http_prefix=http_prefix ? strdup(http_prefix):NULL;
}

void htmlfilter_set_mailto_prefix(struct htmlfilter_info *p,
				  const char *mailto_prefix)
{
	if (p->mailto_prefix)
		free(p->mailto_prefix);

	p->mailto_prefix=mailto_prefix ? strdup(mailto_prefix):NULL;
}

void htmlfilter_set_convertcid(struct htmlfilter_info *p,
			       char *(*convert_cid_func)(const char *, void *),
			       void *convert_cid_func_arg)
{
	p->convert_cid_func=convert_cid_func;
	p->convert_cid_func_arg=convert_cid_func_arg;
}

void htmlfilter(struct htmlfilter_info *p,
		const char32_t *str, size_t cnt)
{
	while (cnt)
	{
		size_t n=(*p->handler_func)(p, str, cnt);

		str += n;
		cnt -= n;
	}
}

/*
** Output HTML text content
*/

static void output(struct htmlfilter_info *p,
		   const char32_t *uc,
		   size_t cnt)
{
	if (cnt && !p->n_discarded)
		(*p->output_func)(uc, cnt, p->output_func_arg);
}

/*
** Output HTML text content given as iso-8859-1 chars.
*/
static void output_chars(struct htmlfilter_info *p,
			 const char *str,
			 size_t cnt)
{
	char32_t unicode_buf[256];

	while (cnt)
	{
		size_t n=sizeof(unicode_buf)/sizeof(unicode_buf[0]), i;

		if (n > cnt)
			n=cnt;

		for (i=0; i<n; ++i)
			unicode_buf[i]=(unsigned char)str[i];

		str += n;
		cnt -= n;
		output(p, unicode_buf, n);
	}
}

/*
** HANDLER: Text content.
*/

static size_t handle_chars(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
		switch (uc[i]) {
		case '&':
			if (i)
			{
				output(p, uc, i);
				return i;
			}

			unicode_buf_clear(&p->atom);

			unicode_buf_append(&p->atom, uc+i, 1);
			p->handler_func=handle_entity;
			return 1;
		case '<':
			if (i)
			{
				output(p, uc, i);
				return i;
			}
			p->handler_func=seen_lt;

			free_attrs(p);
			return 1;

		case '>':
			if (i)
				output(p, uc, i);
			{
				static const char32_t gt[]=
					{'&','g','t',';'};

				output(p, gt, 4);
			}
			return i+1;
		}

	output(p, uc, cnt);
	return cnt;
}

/*
** Convert alphanumeric to lowercase.
**
** Returns: non-zero US-ASCII lowercase value of passed character if the
** passed character is US-ASCII alphabetic or numeric, 0 otherwise.
*/
static char32_t isualnum(char32_t c)
{
	if (c >= 'a' && c <= 'z')
		return c;

	if (c >= 'A' && c <= 'Z')
		return c + ('a'-'A');

	if (c >= '0' && c <= '9')
		return c;

	return 0;
}

/*
** HANDLER: html entity.
*/

static size_t handle_entity(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt)
{
	size_t i;

	if (unicode_buf_len(&p->atom) == 1 && *uc == '#')
	{
		unicode_buf_append(&p->atom, uc, 1);
		return 1;
	}

	for (i=0; i<cnt; ++i)
	{
		char32_t c=isualnum(uc[i]);

		if (c != 0)
		{
			unicode_buf_append(&p->atom, &c, 1);
			continue;
		}

		p->handler_func=handle_chars;
		if (uc[i] == ';')
		{
			/*
			** It's well-formed
			*/
			output(p, unicode_buf_ptr(&p->atom),
			       unicode_buf_len(&p->atom));
			output_chars(p, ";", 1);
			return ++i;
		}

		break;
	}
	return i;
}

/*
** HANDLER: first character after an <
*/

static size_t seen_lt(struct htmlfilter_info *p,
		      const char32_t *uc,
		      size_t cnt)
{
	if (*uc == '?')
	{
		p->handler_func=seen_pi;
		return 1;
	}

	if (*uc == '!')
	{
		p->handler_func=seen_ltexcl;
		return 1;
	}

	unicode_buf_clear(&p->atom);
	p->handler_func=seen_ltspace;
	return seen_ltspace(p, uc, cnt);
}

/*
** HANDLER: "<!"
*/

static size_t seen_ltexcl(struct htmlfilter_info *p,
			  const char32_t *uc,
			  size_t cnt)
{
	if (*uc == '-')
	{
		/* Assume an SGML comment */

		p->handler_func=seen_comment;

		return seen_comment(p, uc, cnt);
	}

	p->handler_func=seen_sgentity;
	return seen_sgentity(p, uc, cnt);
}

/*
** HANDLER: "<! ..."
*/

static size_t seen_sgentity(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; i++)
		if (uc[i] == '>')
		{
			p->handler_func=handle_chars;

			return i+1;
		}

	return i;
}

/*
** HANDLER: "<" followed by whitespace
*/

static size_t seen_ltspace(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt)
{
	if (SPACE(*uc))
		return 1;

	if (*uc == '/')
	{
		p->handler_func=seen_closing_elem;
		return 1;
	}

	if (isualnum(*uc))
	{
		p->handler_func=seen_opening_elem;
		return seen_opening_elem(p, uc, cnt);
	}

	/* Syntax error, punt */

	p->handler_func=handle_chars;
	return handle_chars(p, uc, cnt);
}

/*
** HANDLER: <?
*/

static size_t seen_pi(struct htmlfilter_info *p,
		      const char32_t *uc,
		      size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		if (uc[i] == '?')
		{
			p->handler_func=seen_piq;
			return i+1;
		}
	}
	return i;
}

/*
** HANDLER: <? .... ?
*/

static size_t seen_piq(struct htmlfilter_info *p,
		       const char32_t *uc,
		       size_t cnt)
{
	p->handler_func=seen_pi;

	if (*uc == '>')
	{
		p->handler_func=handle_chars;
		return 1;
	}

	/* Look for the next ? */

	return seen_pi(p, uc, cnt);
}

/*
** HANDLER: Seen <!
*/

static size_t seen_comment(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		if (uc[i] == '-')
		{
			p->handler_func=seen_commentdash;
			return i+1;
		}
	}
	return i;
}

/*
** HANDLER: Seen <! .... -
*/

static size_t seen_commentdash(struct htmlfilter_info *p,
			       const char32_t *uc,
			       size_t cnt)
{
	if (*uc == '-')
	{
		p->handler_func=seen_commentdashdash;
		return 1;
	}
	p->handler_func=seen_comment;
	return seen_comment(p, uc, cnt);
}

/*
** HANDLER: Seen <! .... --
*/

static size_t seen_commentdashdash(struct htmlfilter_info *p,
				   const char32_t *uc,
				   size_t cnt)
{
	if (*uc == '>')
	{
		p->handler_func=handle_chars;
		return 1;
	}

	p->handler_func=seen_commentdash;
	return seen_commentdash(p, uc, cnt);
}

/*
** Comparison function for bsearch() when searching the tags array.
*/

static int search_tags(const void *key, const void *elem)
{
	size_t i;
	const char *cp=((const struct taginfo *)elem)->tagname;
	char32_t c;
	const struct unicode_buf *ukey=(struct unicode_buf *)key;
	const char32_t *k=unicode_buf_ptr(ukey);
	size_t kl=unicode_buf_len(ukey);

	for (i=0; (c=i >= kl ? 0:k[i]) != 0 || cp[i] != 0; ++i)
	{
		char32_t c2=(unsigned char)cp[i];

		if (c < c2)
			return -1;

		if (c > c2)
			return 1;
	}
	return 0;
}

/*
** Sometimes we may need to change one element into another one.
*/

static const struct taginfo *change_element(const struct taginfo *tag)
{
	if (strcmp(tag->tagname, "base") == 0)
		return &div_tag;

	if (strcmp(tag->tagname, "script") == 0 ||
	    strcmp(tag->tagname, "style") == 0)
		return &span_discard_tag;
	return tag;
}

/*
** Emit text, escaping special characters.
*/

static void output_escaped(struct htmlfilter_info *p,
			   const char32_t *uc,
			   size_t cnt)
{
	while (cnt)
	{
		size_t i;

		for (i=0; i<cnt; i++)
			if (uc[i] < ' ' || uc[i] > 127 ||
			    uc[i] == '<' || uc[i] == '>' || uc[i] == '&' ||
			    uc[i] == '"')
				break;

		if (i)
			output(p, uc, i);

		uc += i;
		cnt -= i;

		if (cnt)
		{
			char32_t c;
			char buf[sizeof(char32_t)*2+4];
			char *cp;

			c= *uc++;
			--cnt;

			cp=buf+sizeof(buf)-1;
			*cp=0;
			*--cp=';';
			do
			{
				*--cp=hex[c & 15];
				c /= 16;
			} while (c);
			*--cp='x';
			*--cp='#';
			*--cp='&';

			output_chars(p, cp, strlen(cp));
		}
	}
}

/*
** Completed parsing of a tag.
*/

static void open_element(struct htmlfilter_info *p)
{
	size_t i=0;
	int discard_was_increased=0;

	p->tag=change_element(p->tag);

	if (p->n_open_elements >=
	    sizeof(p->open_elements)/sizeof(p->open_elements[0]))
		return; /* Too many open elements */

	if ((p->tag->flags & FLAG_DISCARD) || p->n_discarded)
	{
		++p->n_discarded;
		discard_was_increased=1;
	}

	if (p->tag->flags & FLAG_NOENDTAG)
		p->tag_empty=1; /* Make it so, Number One. */

	if (p->tag->flags & FLAG_NOPRINT)
		++p->n_discarded; /* Temporary */

	p->open_elements[p->n_open_elements++]=p->tag;

	/*
	** For A elements, the title attribute will have the full target
	** URL. Attempt to extract the hostname and show it before the
	** A element.
	*/

	if (strcmp(p->tag->tagname, "a") == 0)
	{
		size_t i;

		for (i=0; i<p->attrs_index; ++i)
		{
			if (unicode_buf_cmp_str(&p->attrs[i].name, "title", 5)
			    == 0)
			{
				size_t j, k;

				for (j=0; j<unicode_buf_len(&p->attrs[i].value);
				     ++j)
				{
					if (unicode_buf_ptr(&p->attrs[i].value)
					    [j] == ':')
					{
						++j;
						break;
					}
				}

				while (j<unicode_buf_len(&p->attrs[i].value) &&
				       unicode_buf_ptr(&p->attrs[i].value)[j]
				       == '/')
					++j;
				k=j;

				while (k<unicode_buf_len(&p->attrs[i].value))
				{
					switch (unicode_buf_ptr(&p->attrs[i]
								.value)[k]) {
					case '/':
					case '?':
					case '#':
						break;
					default:
						++k;
						continue;
					}
					break;
				}

				if (k > j)
				{
					static const char span[]=
						"<span class=\"urlhost\">[";

					output_chars(p, span,
						     sizeof(span)-1);
					output_escaped(p,
						       unicode_buf_ptr(&p->
								       attrs[i]
								       .value)
						       +j, k-j);

					output_chars(p, "]</span>", 8);
				}
				break;
			}
		}
	}

	output_chars(p, "<", 1);
	output_chars(p, p->tag->tagname, strlen(p->tag->tagname));

	for (i=0; i<p->attrs_index; ++i)
	{
		output_chars(p, " ", 1);
		output(p, unicode_buf_ptr(&p->attrs[i].name),
		       unicode_buf_len(&p->attrs[i].name));

		if (unicode_buf_len(&p->attrs[i].value) > 0)
		{
			output_chars(p, "=\"", 2);

			output_escaped(p, unicode_buf_ptr(&p->attrs[i].value),
				       unicode_buf_len(&p->attrs[i].value));
			output_chars(p, "\"", 1);
		}
	}

	if (p->tag_empty)
		output_chars(p, " /", 2);

	output_chars(p, ">", 1);

	if (p->tag_empty)
	{
		/* This tag did not really open */

		--p->n_open_elements;

		if (discard_was_increased)
			--p->n_discarded;
	}

	if (!p->tag_empty && p->tag->flags & FLAG_BLOCKQUOTE_CITE)
	{
		static const char str[]="<div class=\"quotedtext\">";

		output_chars(p, str, sizeof(str)-1);
	}

	if (p->tag->flags & FLAG_NOPRINT)
		--p->n_discarded; /* Was temporary */
}

/* Close an element */

static void close_element(struct htmlfilter_info *p,
			  const struct taginfo *tag)
{
	size_t i;

	tag=change_element(tag);

	/* Search for the tag that we are closing */

	i=p->n_open_elements;

	while (i)
	{
		if (strcmp(p->open_elements[i-1]->tagname, tag->tagname) == 0)
			break;
		--i;
	}

	if (!i)
		return; /* Did not find a matching open element */

	close_elements_until(p, --i);
}

static void close_elements_until(struct htmlfilter_info *p, size_t i)
{
	while (p->n_open_elements > i)
	{
		--p->n_open_elements;

		if (!p->n_discarded &&
		    (p->open_elements[p->n_open_elements]->flags & FLAG_NOPRINT)
		    == 0)
		{
			const char *cp=
				p->open_elements[p->n_open_elements]->tagname;

			if (p->open_elements[p->n_open_elements]->flags &
			    p->tag->flags & FLAG_BLOCKQUOTE_CITE)
				output_chars(p, "</div>", 6);

			output_chars(p, "</", 2);
			output_chars(p, cp, strlen(cp));
			output_chars(p, ">", 1);
		}

		if (p->n_discarded)
			--p->n_discarded;
	}
}

/*
** HANDLER: Seen </
*/

static size_t seen_closing_elem(struct htmlfilter_info *p,
				const char32_t *uc,
				size_t cnt)
{
	size_t i;
	char32_t c;

	for (i=0; i<cnt; ++i)
	{
		if (uc[i] == '>')
		{
			const struct taginfo *tag;

			p->handler_func=handle_chars;

			tag=bsearch(&p->atom,
				    tags,
				    sizeof(tags)/sizeof(tags[0]),
				    sizeof(tags[0]),
				    search_tags);

			/*
			** Change unknown elements to a <span>
			*/

			if (!tag)
				tag= &unknown_tag;

			close_element(p, tag);
			return i+1;
		}

		/* Loose parsing - ignore spaces wherever they are */

		if (SPACE(uc[i]))
			continue;

		if ((c=uc[i]) == ':' || (c=isualnum(c)) != 0)
		{
			unicode_buf_append(&p->atom, &c, 1);
			continue;
		}

		/*
		** Syntax error, punt.
		*/

		p->handler_func=handle_chars;
		return i;
	}

	return i;
}

/*
** HANDLER: <  [expecting tag
**
** Collect element name.
*/

static size_t seen_opening_elem(struct htmlfilter_info *p,
				const char32_t *uc,
				size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		char32_t c;

		if ((c=uc[i]) == ':' || (c=isualnum(c)) != 0)
		{
			unicode_buf_append(&p->atom, &c, 1);
			continue;
		}

		/*
		** End of element name.
		*/

		p->tag=bsearch(&p->atom,
			       tags,
			       sizeof(tags)/sizeof(tags[0]),
			       sizeof(tags[0]),
			       search_tags);

		/*
		** Change unknown elements to a <span>
		*/

		if (!p->tag)
			p->tag= &unknown_tag;

		p->handler_func=seen_attr;
		p->tag_empty=0;
		return i;
	}
	return i;
}

static void save_attr(struct htmlfilter_info *p);

/*
** HANDLER: expecting attribute name or >
*/

static size_t seen_attr(struct htmlfilter_info *p,
			const char32_t *uc,
			size_t cnt)
{
	if (SPACE(*uc))
		return 1;

	if (*uc == '/')
	{
		p->tag_empty=1;
		return 1;
	}

	if (isualnum(*uc))
	{
		unicode_buf_clear(&p->atom);
		p->handler_func=seen_attrname;
		return seen_attrname(p, uc, cnt);
	}

	p->handler_func=handle_chars;

	if (*uc == '>')
		open_element(p);

	return 1;
}

/*
** After munging a URL, append the original URL, using URL-escaping.
*/

static void append_orig_href(struct htmlfilter_info *p,
			     struct unicode_buf *dst,
			     const char *url)
{
	size_t n=strlen(url);

	while (n)
	{
		size_t i;

		for (i=0; i<n; i++)
			if (!isualnum(url[i]))
				break;

		if (i == 0)
		{
			char32_t b[3];

			b[0]='%';
			b[1]=hex[ (url[0] >> 4) & 15];
			b[2]=hex[ url[0] & 15];

			unicode_buf_append(dst, b, 3);
			++url;
			--n;
			continue;
		}

		unicode_buf_append_char(dst, url, i);
		url += i;
		n -= i;
	}
}

/*
** Munge an HREF url accordingly.
**
** Returns non-0 if the URL was recognized and munged.
**
** A 0 return means that I do not understand what this URL is, so it should
** be omitted.
*/

static int change_href(struct htmlfilter_info *p,
		       char *url,
		       struct unicode_buf *dst,
		       int must_be_cid, /* Understand only CID: urls */
		       int *was_http_url
		       /* Set to non-0 if the munged URL was http or https */
		       )
{
	size_t i;

	*was_http_url=0;

	/* Convert the method to lowercase */

	for (i=0; url[i] && url[i] != ':'; ++i)
	{
		if (url[i] >= 'A' && url[i] <= 'Z')
			url[i] += 'a'-'A';
	}

	if (strncmp(url, "cid:", 4) == 0 && p->convert_cid_func)
	{
		char *q;

		if ((q=(*p->convert_cid_func)
		     (url+4, p->convert_cid_func_arg)) != NULL)
		{
			unicode_buf_append_char(dst, q, strlen(q));
			free(q);
			return 1;
		}
	}

	if (must_be_cid)
		return 0;

	if ((strncmp(url, "http:", 5) == 0 ||
	     strncmp(url, "https:", 6) == 0)
	    && p->http_prefix && *p->http_prefix)
	{
		*was_http_url=1;
		unicode_buf_append_char(dst, p->http_prefix, strlen(p->http_prefix));
		append_orig_href(p, dst, url);
		return 1;
	}

	if (strncmp(url, "mailto:", 7) == 0
	    && p->mailto_prefix && *p->mailto_prefix)
	{
		size_t i;

		for (i=0; url[i]; ++i)
			if (url[i] == '?')
			{
				url[i]='&';
				break;
			}

		unicode_buf_append_char(dst, p->mailto_prefix,
					strlen(p->mailto_prefix));
		append_orig_href(p, dst, url+7);
		return 1;
	}

	return 0;
}

/*
** Completed parsing of attribute[=value]?
**
** If value was provided, malloc a buffer for it, copy it, put it into
** cur_attr->value.
*/

static void save_attr_int(struct htmlfilter_info *p,
			  struct unicode_buf *name,
			  struct unicode_buf *value)
{
	struct attr *cur_attr;

	if (p->attrs_index >= sizeof(p->attrs)/sizeof(p->attrs[0]))
		return;

	cur_attr=p->attrs + p->attrs_index;

	++p->attrs_index;

	unicode_buf_init_copy(&cur_attr->name, name);
	unicode_buf_init_copy(&cur_attr->value, value);
}

static int is_attr(struct htmlfilter_info *p, const char *c)
{
	return unicode_buf_cmp_str(&p->atom, c, strlen(c)) == 0;
}

/*
** Convert the current attribute that contains a URL to utf-8, if necessary
** and resolve against contentbase, if necessary.
*/
static char *resolve_url(struct htmlfilter_info *p)
{
	char *buf;
	size_t size;
	char *cp;

	unicode_convert_handle_t h=
		unicode_convert_fromu_init("utf-8", &buf, &size, 1);

	if (h)
	{
		unicode_convert_uc(h, unicode_buf_ptr(&p->value),
				     unicode_buf_len(&p->value));

		if (unicode_convert_deinit(h, NULL))
			buf=NULL;
	}
	else
	{
		buf=NULL;
	}

	if (!buf)
		return NULL;

	if (p->contentbase && *p->contentbase)
	{
		cp=rfc2045_append_url(p->contentbase, buf);

		free(buf);
		buf=cp;
	}
	return (buf);
}

/*
** Take the contents of an HREF (or a SRC), prepend contentbase, if necessary
** then invoke change_href() and save the result as the replacement
** HREF/SRC attribute.
**
** Returns the original HREF/SRC was HTTP or HTTPS url in the malloc-ed
** buffer, or NULL if the HREF/SRC was not http or https (but something else).
*/

static char *handle_url(struct htmlfilter_info *p,
			int must_be_cid)
{
	struct unicode_buf new_href;
	char *cp;
	int http_url;

	char *retval=NULL;

	if ((cp=resolve_url(p)) == NULL)
		return NULL;

	unicode_buf_init(&new_href, (size_t)-1);

	if (change_href(p, cp, &new_href, must_be_cid, &http_url))
	{
		save_attr_int(p, &p->atom, &new_href);

		if (!http_url)
		{
			free(cp);
			cp=NULL;
		}

		retval=cp;
		cp=NULL;
	}

	if (cp)
		free(cp);

	unicode_buf_deinit(&new_href);
	return retval;
}

/*
** If this is the second occurence of the same attribute, nuke it.
** Only one occurence of each attribute.
*/

static int attr_already_exists(struct htmlfilter_info *p,
			       struct unicode_buf *name)
{
	size_t i;

	for (i=0; i<p->attrs_index; ++i)
	{
		if (unicode_buf_cmp(&p->attrs[i].name, name) == 0)
			return 1;
	}
	return 0;
}

static void save_attr(struct htmlfilter_info *p)
{
	p->handler_func=seen_attr;

	if (attr_already_exists(p, &p->atom))
		return;

	/*
	** Transform <blockquote type="cite"> into
	**
	** <blockquote class="citeN"> where N nests from 0 to 2.
	*/

	if (is_attr(p, "type") && strcmp(p->tag->tagname, "blockquote") == 0 &&
	    unicode_buf_len(&p->value) == 4)
	{
		size_t i;

		for (i=0; i<4; ++i)
			if (isualnum(unicode_buf_ptr(&p->value)[i])
			    != "cite"[i])
				break;

		if (i == 4)
		{
			size_t n=0, j;
			char buf[10];

			for (j=0; j<p->n_open_elements; ++j)
				if (p->open_elements[j]->flags &
				    FLAG_BLOCKQUOTE_CITE)
					++n;

			p->tag=&blockquote_cite_tag;

			sprintf(buf, "cite%d", (int)(n % 3));

			unicode_buf_clear(&p->value);
			unicode_buf_append_char(&p->value, buf, strlen(buf));

			unicode_buf_clear(&p->atom);
			unicode_buf_append_char(&p->atom, "class", 5);

			if (!attr_already_exists(p, &p->atom))
			{
				save_attr_int(p, &p->atom, &p->value);
				return;
			}
		}
	}

	/*
	** Do not allow title attributes on an A element, we'll supply our
	** own.
	*/

	if (is_attr(p, "title") &&
	    strcmp(p->tag->tagname, "a") == 0)
		return;

	if (is_attr(p, "lang")
	    || is_attr(p, "title")
	    || is_attr(p, "dir")
	    || is_attr(p, "size")
	    || is_attr(p, "color")
	    || is_attr(p, "face")

	    || is_attr(p, "span")
	    || is_attr(p, "width")
	    || is_attr(p, "height")
	    || is_attr(p, "align")
	    || is_attr(p, "char")
	    || is_attr(p, "charoff")
	    || is_attr(p, "valign")
	    || is_attr(p, "alt")
	    )
	{
		/* Safe attributes */

		save_attr_int(p, &p->atom, &p->value);
		return;
	}

	if (is_attr(p, "src") && strcmp(p->tag->tagname, "img") == 0)
	{
		char *url=handle_url(p, 1);

		if (url)
			free(url);
		return;
	}

	if (is_attr(p, "href"))
	{
		if (strcmp(p->tag->tagname, "base") == 0)
		{
			char *buf=malloc(unicode_buf_len(&p->value)+1);

			if (buf)
			{
				size_t i;

				for (i=0; i<unicode_buf_len(&p->value); ++i)
				{
					buf[i]=unicode_buf_ptr(&p->value)[i];
				}
				buf[i]=0;

				htmlfilter_set_contentbase(p, buf);
				free(buf);
			}
			return;
		}


		if (strcmp(p->tag->tagname, "a") == 0)
		{
			char *url;

			if ((url=handle_url(p, 0)) != NULL)
			{
				/* Append target=_blank to HREF */

				unicode_buf_clear(&p->atom);
				unicode_buf_append_char(&p->atom, "target", 6);
				unicode_buf_clear(&p->value);
				unicode_buf_append_char(&p->value, "_blank", 6);
				save_attr_int(p, &p->atom, &p->value);

				/* Append the full URL in the title tag */

				unicode_buf_clear(&p->atom);
				unicode_buf_append_char(&p->atom, "title", 5);
				unicode_buf_clear(&p->value);
				unicode_buf_append_char(&p->value, url, strlen(url));
				save_attr_int(p, &p->atom, &p->value);
				free(url);

			}
			return;
		}
	}
}

/*
** HANDLER: reading attribute name.
*/

static size_t seen_attrname(struct htmlfilter_info *p,
			    const char32_t *uc,
			    size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		char32_t c;

		if ((c=uc[i]) == ':' || c == '-' || (c=isualnum(c)) != 0)
		{
			unicode_buf_append(&p->atom, &c, 1);
			continue;
		}

		unicode_buf_clear(&p->value);
		p->value_quote=0;

		p->handler_func=seen_attr; /* No value expected */

		if (uc[i] == '=')
		{
			p->handler_func=seen_attrvalue_1stchar;
			return ++i;
		}
		save_attr(p);
		return i;
	}
	return cnt;
}

/*
** HANDLER: expecting first character of the attribute's value.
*/

static size_t seen_attrvalue_1stchar(struct htmlfilter_info *p,
				     const char32_t *uc,
				     size_t cnt)
{
	p->handler_func=seen_attrvalue;

	switch (*uc) {
	case '\'':
	case '\"':
		p->value_quote= *uc;
		return 1;
	}

	return seen_attrvalue(p, uc, cnt);
}

/*
** HANDLER: expecting the value of an attribute.
*/

static size_t seen_attrvalue(struct htmlfilter_info *p,
			     const char32_t *uc,
			     size_t cnt)
{
	size_t i;

	for (i=0; i<cnt; ++i)
	{
		if (uc[i] == '&')
		{
 			unicode_buf_append(&p->value, uc, i);
			unicode_buf_clear(&p->atom2);
			p->handler_func=seen_attrvalue_entity;
			return i+1;
		}

		/*
		** If the first char in the value is ' or ", another one ends
		** the value. Otherwise, the value gets ended by a / or >
		*/

		if (p->value_quote)
		{
			if (uc[i] == p->value_quote)
			{
				unicode_buf_append(&p->value, uc, i);
				save_attr(p);
				return i+1;
			}
		}
		else if (SPACE(uc[i]) || uc[i] == '/' || uc[i] == '>')
		{
			unicode_buf_append(&p->value, uc, i);
			save_attr(p);
			return i;
		}
	}
	unicode_buf_append(&p->value, uc, i);
	return cnt;
}

/*
** atom2 should contain one of:
**
**  #<decimal value>
**  #[xX]<hex value>
**  <entity>
*/

static void append_entity(struct htmlfilter_info *p)
{
	char32_t v=0;

	if (unicode_buf_len(&p->atom2) &&
	    unicode_buf_ptr(&p->atom2)[0] == '#')
	{
		const char32_t *u=unicode_buf_ptr(&p->atom2);
		size_t n=unicode_buf_len(&p->atom2);

		++u;
		--n;

		if (n && (*u == 'x' || *u == 'X'))
		{
			while (--n)
			{
				char32_t c=*++u;
				const char *cp;

				if (c >= 'a' && c <= 'f')
					c += 'A'-'a';

				if (c < ' ' || c > 127)
					break;

				cp=strchr(hex, c);

				if (!cp)
					break;

				v = v * 16 + (cp-hex);
			}
		}
		else
		{
			while (n)
			{
				char32_t c= *u++;

				--n;

				if (c < '0' || c > '9')
					break;

				v = v * 10 + (c-'0');
			}
		}
	}
	else
	{
		char entitybuf[32];
		size_t i;

		if (unicode_buf_len(&p->atom2) >= sizeof(entitybuf))
			return;

		for (i=0; i<unicode_buf_len(&p->atom2); ++i)
		{
			char32_t c=unicode_buf_ptr(&p->atom2)[i];

			if ((unsigned char)c != c)
				return;
			entitybuf[i]=c;
		}
		entitybuf[i]=0;

		if ((v=unicode_html40ent_lookup(entitybuf)) == 0)
			return;
	}

	unicode_buf_append(&p->value, &v, 1);
}

/*
** HANDLER: &entity in an attribute.
**
** We generally expect &name; or &#name;
**
** However there's plenty of broken HTML that does not &-escape attribute
** values containing URLs.
*/

static size_t seen_attrvalue_entity(struct htmlfilter_info *p,
				    const char32_t *uc,
				    size_t cnt)
{
	size_t i;

	if (unicode_buf_len(&p->atom2) == 0 && *uc == '#')
	{
		unicode_buf_append(&p->atom2, uc, 1);
		return 1;
	}

	for (i=0; i<cnt; ++i)
	{
		char32_t c=isualnum(uc[i]);

		if (c)
		{
			unicode_buf_append(&p->atom2, uc+i, 1);
			continue;
		}

		switch (uc[i]) {
		case ';':
			append_entity(p);
			++i;
			break;
		case '&':
		case '=':

			/* Broken URL, most likely */

			{
				char32_t amp='&';

				unicode_buf_append(&p->value, &amp, 1);
			}
			unicode_buf_append_buf(&p->value, &p->atom2);
			break;
		default:
			/* Not ...&foo;..., not ...&foo&..., not ...&foo=... */

			/* forget the whole thing */
			break;
		}
		p->handler_func=seen_attrvalue;
		return i;
	}
	return cnt;
}
