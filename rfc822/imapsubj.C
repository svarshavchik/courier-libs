/*
** Copyright 2000-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"config.h"
#include	<stdio.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	<string.h>
#if	HAVE_STRINGS_H
#include	<strings.h>
#endif
#include	"rfc822.h"
#include	"rfc2047.h"
#include	<string>
#include	<string_view>
#include	<optional>
#include	<tuple>
#include	<algorithm>
#include	<courier-unicode.h>

/* Skip over blobs */

static std::string_view skipblob(
	std::string_view p,
	std::optional<std::string> &blob_out)
{
	if (p.empty() || p[0] != '[')
		return p;

	bool isalldigits{true};

	size_t i;

	for (i=1; i < p.size(); ++i)
	{
		if (p[i] == '[' || p[i] == ']')
			break;
		if (p[i] < '0' || p[i] > '9')
			isalldigits=false;
	}

	if (i == p.size() || p[i] != ']')
		return p;

	++i;

	while (i < p.size() && unicode_isspace(p[i]))
		++i;

	if (!isalldigits && blob_out)
		*blob_out += p.substr(0, i);
	p.remove_prefix(i);

	return p;
}

static std::string_view skipblobs(
	std::string_view p,
	std::optional<std::string> &blob_out)
{
	std::string_view q=p;

	do
	{
		p=q;
		q=skipblob(p, blob_out);
	} while (q != p);
	return (q);
}

static bool prefixcmp(std::string_view str, std::string_view pfix)
{
	if (str.size() < pfix.size())
		return false;

	return std::equal(str.data(), str.data()+pfix.size(), pfix.data(),
		[](auto a, auto b)
		{
			return unicode_lc(static_cast<unsigned char>(a))
				== unicode_lc(static_cast<unsigned char>(b));
		}
	);
}

/* Remove artifacts from the subject header */

static std::tuple<std::string_view, int> stripsubj(
	std::string &str,
	std::optional<std::string> &blob_out)
{
	std::string dummy_blob;
	std::string &blob_ptr=
		blob_out ? *blob_out : dummy_blob;

	int hasrefwd=0;

	{
		auto p=str.begin(), q=p;

		while (p != str.end())
		{
			if (!unicode_isspace(*p))
			{
				*q++=*p++;
				continue;
			}
			while (p+1 != str.end() && unicode_isspace(*(p+1)))
			{
				++p;
			}
			*q++=' ';
			++p;
		}
		str.erase(q, str.end());
	}

	std::string_view p=str;

	bool doit;

	do
	{
		doit=false;
		/*
		**
		** (2) Remove all trailing text of the subject that matches
		** the subj-trailer ABNF, repeat until no more matches are
		** possible.
		**
		**  subj-trailer    = "(fwd)" / WSP
		*/

		while (!p.empty())
		{
			if (unicode_isspace(p.back()))
			{
				p.remove_suffix(1);
				continue;
			}
			if (p.size() >= 5 && prefixcmp(
				p.substr(p.size()-5),
				"(fwd)"
			))
			{
				p.remove_suffix(5);
				hasrefwd |= CORESUBJ_FWD;
				continue;
			}
			break;
		}

		while (!p.empty())
		{
			for (;;)
			{
				int flag=0;
				/*
				**
				** (3) Remove all prefix text of the subject
				** that matches the subj-leader ABNF.
				**
				**   subj-leader     = (*subj-blob subj-refwd) / WSP
				**
				**   subj-blob       = "[" *BLOBCHAR "]" *WSP
				**
				**   subj-refwd      = ("re" / ("fw" ["d"])) *WSP [subj-blob] ":"
				**
				**   BLOBCHAR        = %x01-5a / %x5c / %x5e-7f
				**                   ; any CHAR except '[' and ']'
				*/

				if (unicode_isspace(p.front()))
				{
					p.remove_prefix(1);
					continue;
				}

				std::optional<std::string> blob;
				std::string_view q=skipblobs(p, blob);

				if (prefixcmp(q, "re"))
				{
					q.remove_prefix(2);
					flag=CORESUBJ_RE;
				}
				else if (prefixcmp(q, "fwd"))
				{
					q.remove_prefix(3);
					flag=CORESUBJ_FWD;
				}
				else if (prefixcmp(q, "fw"))
				{
					q.remove_prefix(2);
					flag=CORESUBJ_FWD;
				}

				if (flag)
				{
					size_t orig_size=blob_ptr.size();

					q=skipblob(q, blob_out);

					if (q.substr(0, 1) == ":")
					{
						p=q.substr(1);
						hasrefwd |= flag;
						continue;
					}
					blob_ptr.erase(orig_size);
				}

				/*
				**
				** (4) If there is prefix text of the subject
				** that matches the subj-blob ABNF, and
				** removing that prefix leaves a non-empty
				** subj-base, then remove the prefix text.
				**
				**   subj-base       = NONWSP *([*WSP] NONWSP)
				**                   ; can be a subj-blob
				*/

				size_t orig_size=blob_ptr.size();
				q=skipblob(p, blob);

				if (q != p && !q.empty())
				{
					p=q;
					continue;
				}
				blob_ptr.erase(orig_size);
				break;
			}

			/*
			**
			** (6) If the resulting text begins with the
`			** subj-fwd-hdr ABNF and ends with the subj-fwd-trl
			** ABNF, remove the subj-fwd-hdr and subj-fwd-trl and
			** repeat from step (2).
			**
			**   subj-fwd-hdr    = "[fwd:"
			**
			**   subj-fwd-trl    = "]"
			*/
			if (prefixcmp(p, "[fwd:"))
			{
				auto rb=p.find(']');
				if (rb == p.size()-1)
				{
					p.remove_suffix(1);
					p.remove_prefix(5);
					hasrefwd |= CORESUBJ_FWD;

					doit=true;
				}
			}
			break;
		}
	} while (doit);

	return {p, hasrefwd};
}

std::tuple<std::string, int> rfc822::coresubj(std::string_view str)
{
	std::u32string us;

	display_header_unicode("subject", str, std::back_inserter(us));

	for (auto &c:us)
		c=unicode_uc(c);

	auto s=unicode::iconvert::fromu::convert(us, unicode::utf_8).first;

	std::optional<std::string> blob;

	auto [sv, hasrefwd]=stripsubj(s, blob);

	return {std::string{sv.begin(), sv.end()}, hasrefwd};
}

std::tuple<std::string, int> rfc822::coresubj_nouc(std::string_view str)
{
	std::string s{str.begin(), str.end()};

	std::optional<std::string> blob;

	auto [sv, hasrefwd]=stripsubj(s, blob);

	return {std::string{sv.begin(), sv.end()}, hasrefwd};
}

std::tuple<std::string, int> rfc822::coresubj_keepblobs(std::string_view str)
{
	std::string s{str.begin(), str.end()};

	std::optional<std::string> blob;

	blob.emplace();
	auto [sv, hasrefwd]=stripsubj(s, blob);

	blob->append(sv);
	return {std::move(*blob), hasrefwd};
}

char *rfc822_coresubj(const char *s, int *hasrefwd_ret)
{
	auto [str, hasrefwd] = rfc822::coresubj(s);

	if (hasrefwd_ret)
		*hasrefwd_ret=hasrefwd;
	return strdup(str.c_str());
}

char *rfc822_coresubj_nouc(const char *s, int *hasrefwd_ret)
{
	auto [str, hasrefwd] = rfc822::coresubj_nouc(s);

	if (hasrefwd_ret)
		*hasrefwd_ret=hasrefwd;
	return strdup(str.c_str());
}

char *rfc822_coresubj_keepblobs(const char *s)
{
	auto [str, hasrefwd] = rfc822::coresubj_keepblobs(s);

	return strdup(str.c_str());
}
