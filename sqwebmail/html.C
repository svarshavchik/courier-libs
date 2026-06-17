/*
** Copyright 2011-2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/

#include "html.h"

#include <courier-unicode.h>
#include "rfc2045/rfc2045.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>

#define SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')

#include <fstream>

struct taginfo {

	char tagname[16];
	int flags;
};

static const char hex[]="0123456789ABCDEF";


#define FLAG_NOENDTAG	0x01
#define FLAG_DISCARD	0x02
#define FLAG_NOPRINT	0x04

#define FLAG_BLOCKQUOTE_CITE 0x1000

// This list must be in alphabetical order
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
	std::u32string name; /* Attribute name */
	std::u32string value; /* Attribute value */
};

struct htmlfilter_info {

	/* The output function receives the HTML-filtered stream */

	void (*output_func)(const char32_t *, size_t, void *);
	void *output_func_arg;

	/* Content base for relative URLs */
	std::string contentbase;

	/* Prepend to http: and https: links */
	std::string http_prefix;

	/* Prepent to mailto: links */
	std::string mailto_prefix;

	/* A cid: link gets passed to this function for processing. */
	std::function<std::string (const char *)> convert_cid_func;

	/* Current handle for the input HTML stream */

	size_t (*handler_func)(struct htmlfilter_info *,
			       const char32_t *,
			       size_t);

	/*
	** An & entity name. Or a tag name. Or an attribute name or value.
	*/

	std::u32string atom;

	/*
	** An attribute value
	*/

	std::u32string value;

	/*
	** Another atom
	*/

	std::u32string atom2;

	/*
	** Quoting character
	*/

	char32_t value_quote;

	/* Current tag being processed */
	const struct taginfo *tag=nullptr;

	/* Whether parsed an empty tag */
	bool tag_empty{false};

	std::vector<attr> attrs;

	/*
	** Current list of active elements.
	** We limit the number of open elements to 128
	*/

	const struct taginfo *open_elements[128];
	size_t n_open_elements=0;

	/*
	** How many elements have been open since the first element whose
	** contents should be discarded
	*/

	size_t n_discarded=0;
};

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
	struct htmlfilter_info *p=new htmlfilter_info;

	if (!p)
		return p;

	p->output_func=output_func;
	p->output_func_arg=output_func_arg;

	p->handler_func=handle_chars;
	return p;
}

static void close_elements_until(struct htmlfilter_info *p, size_t i);

void htmlfilter_free(struct htmlfilter_info *p)
{
	delete p;
}

void htmlfilter_set_contentbase(struct htmlfilter_info *p,
				const char *contentbase)
{
	p->contentbase=contentbase;
}


void htmlfilter_set_http_prefix(struct htmlfilter_info *p,
				std::string http_prefix)
{
	p->http_prefix=std::move(http_prefix);
}

void htmlfilter_set_mailto_prefix(struct htmlfilter_info *p,
				  std::string mailto_prefix)
{
	p->mailto_prefix=std::move(mailto_prefix);
}

void htmlfilter_set_convertcid(struct htmlfilter_info *p,
			       std::function<std::string (const char *)> convert_cid_func)
{
	p->convert_cid_func=std::move(convert_cid_func);
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

			p->atom.clear();
			p->atom.push_back(uc[i]);
			p->handler_func=handle_entity;
			return 1;
		case '<':
			if (i)
			{
				output(p, uc, i);
				return i;
			}
			p->handler_func=seen_lt;
			p->attrs.clear();
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

	if (p->atom.size() == 1 && *uc == '#')
	{
		p->atom.push_back(*uc);
		return 1;
	}

	for (i=0; i<cnt; ++i)
	{
		char32_t c=isualnum(uc[i]);

		if (c != 0)
		{
			p->atom.push_back(c);
			continue;
		}

		p->handler_func=handle_chars;
		if (uc[i] == ';')
		{
			/*
			** It's well-formed
			*/
			output(p, p->atom.data(), p->atom.size());
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

	p->atom.clear();
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
** Sometimes we may need to change one element into another one.
*/

static const struct taginfo *change_element(const struct taginfo *tag)
{
	std::string_view tagname=tag->tagname;

	if (tagname == "base")
	{
		return &div_tag;
	}

	if (tagname == "script" ||
	    tagname == "style")
	{
		return &span_discard_tag;
	}
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
	bool discard_was_increased=false;

	if (p->tag->flags & FLAG_NOENDTAG)
		p->tag_empty=true; /* Make it so, Number One. */

	p->tag=change_element(p->tag);

	if (p->n_open_elements >=
	    sizeof(p->open_elements)/sizeof(p->open_elements[0]))
		return; /* Too many open elements */

	if ((p->tag->flags & FLAG_DISCARD) || p->n_discarded)
	{
		++p->n_discarded;
		discard_was_increased=true;
	}

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
		for (auto &attr:p->attrs)
		{
			if (attr.name == U"title")
			{
				size_t j, k;

				for (j=0; j<attr.value.size();
				     ++j)
				{
					if (attr.value[j] == ':')
					{
						++j;
						break;
					}
				}

				while (j<attr.value.size() &&
				       attr.value[j] == '/')
					++j;
				k=j;

				while (k<attr.value.size())
				{
					switch (attr.value[k]) {
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
						       attr.value.data()+j,
						       k-j);

					output_chars(p, "]</span>", 8);
				}
				break;
			}
		}
	}

	output_chars(p, "<", 1);
	output_chars(p, p->tag->tagname, strlen(p->tag->tagname));

	for (auto &attr:p->attrs)
	{
		output(p, U" ", 1);
		output(p, attr.name.data(), attr.name.size());

		if (attr.value.size() > 0)
		{
			output_chars(p, "=\"", 2);

			output_escaped(p, attr.value.data(),
				       attr.value.size());
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

static const taginfo &find_tag(struct htmlfilter_info *p)
{
	auto tag=std::lower_bound(
		std::begin(tags),
		std::end(tags),
		p->atom,
		[]
		(auto &left, auto &right)
		{
			return std::lexicographical_compare(
				left.tagname,
				left.tagname+strlen(left.tagname),
				right.begin(),
				right.end()
			);
		}
	);

	if (!(tag < std::end(tags) &&
			strlen(tag->tagname) == p->atom.size() &&
			std::equal(p->atom.begin(), p->atom.end(),
				tag->tagname)))
	{
		tag=&unknown_tag;
	}

	return *tag;
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
			p->handler_func=handle_chars;

			const taginfo &tag=find_tag(p);
			close_element(p, &tag);
			return i+1;
		}

		/* Loose parsing - ignore spaces wherever they are */

		if (SPACE(uc[i]))
			continue;

		if ((c=uc[i]) == ':' || (c=isualnum(c)) != 0)
		{
			p->atom.push_back(c);
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
			p->atom.push_back(c);
			continue;
		}

		/*
		** End of element name.
		*/

		p->tag=&find_tag(p);

		p->handler_func=seen_attr;
		p->tag_empty=false;
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
		p->tag_empty=true;
		return 1;
	}

	if (isualnum(*uc))
	{
		p->atom.clear();
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
			     std::u32string &dst,
			     std::string_view url)
{
	size_t n=url.size();

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

			dst.append(b, b+3);
			url.remove_prefix(1);
			n -= 1;
			continue;
		}

		dst.append(url.begin(), url.begin()+i);
		url.remove_prefix(i);
		n -= i;
	}
}

/*
** Munge an HREF url accordingly.
**
** Returns true if the URL was recognized and munged.
**
** A false return means that I do not understand what this URL is, so it should
** be omitted.
*/

static bool change_href(struct htmlfilter_info *p,
		       std::string &url,
		       std::u32string &dst,
		       bool must_be_cid, /* Understand only CID: urls */
		       bool &was_http_url
		       /* Set to true if the munged URL was http or https */
		       )
{
	dst.clear();
	was_http_url=false;

	/* Convert the method to lowercase */

	for (size_t i=0; i < url.size() && url[i] != ':'; ++i)
	{
		if (url[i] >= 'A' && url[i] <= 'Z')
			url[i] += 'a'-'A';
	}

	if (std::string_view{url}.substr(0, 4 ) == "cid:"
		&& p->convert_cid_func)
	{
		std::string q=p->convert_cid_func(url.data()+4);

		dst.append(q.begin(), q.end());
		return true;
	}

	if (must_be_cid)
		return false;

	if (((std::string_view{url}.substr(0, 5) == "http:") ||
	    (std::string_view{url}.substr(0, 6) == "https:"))
	    && !p->http_prefix.empty())
	{
		was_http_url=true;
		dst.append(p->http_prefix.begin(), p->http_prefix.end());
		append_orig_href(p, dst, url);
		return true;
	}

	if (std::string_view{url}.substr(0, 7) == "mailto:"
	    && !p->mailto_prefix.empty())
	{
		size_t i;

		for (i=0; i < url.size(); ++i)
			if (url[i] == '?')
			{
				url[i]='&';
				break;
			}

		dst.append(p->mailto_prefix.begin(), p->mailto_prefix.end());
		append_orig_href(p, dst, std::string_view{url}.substr(7));
		return true;
	}

	return false;
}

/*
** Completed parsing of attribute[=value]?
**
** If value was provided, malloc a buffer for it, copy it, put it into
** cur_attr->value.
*/

static void save_attr_int(struct htmlfilter_info *p,
			  const std::u32string &name,
			  const std::u32string &value)
{
	if (p->attrs.size() >= 32)
		return;

	p->attrs.push_back({name, value});
}

static bool is_attr(struct htmlfilter_info *p, const char *c)
{
	return (p->atom.size() == strlen(c)) &&
		std::equal(p->atom.begin(), p->atom.end(), c);
}

/*
** Convert the current attribute that contains a URL to utf-8, if necessary
** and resolve against contentbase, if necessary.
*/
static std::string resolve_url(struct htmlfilter_info *p)
{
	auto buf=unicode::iconvert::fromu::convert(
		p->value,
		unicode::utf_8
	).first;
	if (!p->contentbase.empty())
	{
		buf=rfc2045::append_url(p->contentbase, buf);
	}
	return (buf);
}

/*
** Take the contents of an HREF (or a SRC), prepend contentbase, if necessary
** then invoke change_href() and save the result as the replacement
** HREF/SRC attribute.
**
** Returns the original HREF/SRC was HTTP or HTTPS url, an empty string if the
** HREF/SRC was not http or https (but something else).
*/

static std::string handle_url(struct htmlfilter_info *p, bool must_be_cid)
{
	std::u32string new_href;
	bool http_url;

	std::string retval;

	auto cp=resolve_url(p);

	if (change_href(p, cp, new_href, must_be_cid, http_url))
	{
		save_attr_int(p, p->atom, new_href);
		if (!http_url)
		{
			cp.clear();
		}

		retval=std::move(cp);
	}

	return retval;
}

/*
** If this is the second occurence of the same attribute, nuke it.
** Only one occurence of each attribute.
*/

static bool attr_already_exists(struct htmlfilter_info *p,
			       const std::u32string &name)
{
	for (auto &attr:p->attrs)
	{
		if (attr.name == name)
			return true;
	}
	return false;
}

static void save_attr(struct htmlfilter_info *p)
{
	p->handler_func=seen_attr;

	if (attr_already_exists(p, p->atom))
		return;

	/*
	** Transform <blockquote type="cite"> into
	**
	** <blockquote class="citeN"> where N nests from 0 to 2.
	*/

	if (is_attr(p, "type") && strcmp(p->tag->tagname, "blockquote") == 0 &&
	    p->value.size() == 4)
	{
		size_t i=0;

		for (i=0; i<4; ++i)
			if (isualnum(p->value[i])
			    != static_cast<char32_t>("cite"[i]))
				break;

		if (i == 4)
		{
			size_t n=0, j;

			for (j=0; j<p->n_open_elements; ++j)
				if (p->open_elements[j]->flags &
				    FLAG_BLOCKQUOTE_CITE)
					++n;

			p->tag=&blockquote_cite_tag;

			p->value=U"cite#";
			p->value[4]= '0' + (n % 3);

			p->atom = U"class";
			if (!attr_already_exists(p, p->atom))
			{
				save_attr_int(p, p->atom, p->value);
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
		save_attr_int(p, p->atom, p->value);
		return;
	}

	if (is_attr(p, "src") && strcmp(p->tag->tagname, "img") == 0)
	{
		(void)handle_url(p, true);
		return;
	}

	if (is_attr(p, "href"))
	{
		if (strcmp(p->tag->tagname, "base") == 0)
		{
			std::string buf{p->value.begin(), p->value.end()};

			p->contentbase=std::move(buf);
			return;
		}


		if (strcmp(p->tag->tagname, "a") == 0)
		{
			auto url=handle_url(p, false);

			if (!url.empty())
			{
				/* Append target=_blank to HREF */

				p->atom = U"target";
				p->value = U"_blank";
				save_attr_int(p, p->atom, p->value);

				/* Append the full URL in the title tag */

				p->atom = U"title";
				p->value.clear();
				p->value.append(url.begin(), url.end());
				save_attr_int(p, p->atom, p->value);
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
			p->atom.push_back(c);
			continue;
		}

		p->value.clear();
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
			p->value.append(uc, uc+i);
			p->atom2.clear();
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
				p->value.append(uc, uc+i);
				save_attr(p);
				return i+1;
			}
		}
		else if (SPACE(uc[i]) || uc[i] == '/' || uc[i] == '>')
		{
			p->value.append(uc, uc+i);
			save_attr(p);
			return i;
		}
	}
	p->value.append(uc, uc+i);
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

	if (p->atom2.size() && p->atom2[0] == '#')
	{
		const char32_t *u=p->atom2.data();
		size_t n=p->atom2.size();

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

		if (p->atom2.size() >= sizeof(entitybuf))
			return;

		for (i=0; i<p->atom2.size(); ++i)
		{
			char32_t c=p->atom2[i];

			if ((unsigned char)c != c)
				return;
			entitybuf[i]=c;
		}
		entitybuf[i]=0;

		if ((v=unicode_html40ent_lookup(entitybuf)) == 0)
			return;
	}

	p->value.push_back(v);
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

	if (p->atom2.empty() && *uc == '#')
	{
		p->atom2.push_back(*uc);
		return 1;
	}

	for (i=0; i<cnt; ++i)
	{
		char32_t c=isualnum(uc[i]);

		if (c)
		{
			p->atom2.push_back(c);
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

			p->value.push_back('&');
			p->value.append(p->atom2.begin(), p->atom2.end());
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
