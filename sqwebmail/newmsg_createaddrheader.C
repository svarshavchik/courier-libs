/*
** Copyright 1998 - 2026 S. Varshavchik.  See COPYING for
** distribution information.
*/

#include	"config.h"
#include	"maildir.h"
#include	"addressbook.h"
#include	"rfc822/rfc822.h"
#include	"rfc822/rfc2047.h"
#include	<map>
#include	<set>
#include	<unordered_set>
#include	<list>
extern int newdraftfd;

namespace {
	struct wrap_address_line {

		using iterator_category=std::output_iterator_tag;
		using value_type=void;
		using pointer=void;
		using reference=void;
		using difference_type=void;

		std::string &buf;
		const char *nl="";

		wrap_address_line(std::string &buf) : buf{buf} {}

		wrap_address_line(const wrap_address_line &)=default;
		wrap_address_line &operator=(const wrap_address_line &a)
		{
			nl=a.nl;
			return *this;
		}

		wrap_address_line &operator++() { return *this; }
		wrap_address_line &operator++(int) { return *this; }
		wrap_address_line &operator*() { return *this; }
		wrap_address_line &operator=(std::string s)
		{
			buf += nl;
			buf += std::move(s);
			nl="\n  ";
			return *this;
		}
	};
}

void create_addrheader(
	std::string_view header,
	std::string_view content_utf8
)
{
	rfc822::tokens t{content_utf8};
	rfc822::addresses a{t};

	std::unordered_set<std::string> seen_addresses;
	std::set<std::string> replacements;
	std::list<rfc822::tokens> replacement_tokens;

	for (size_t i=0; i<a.size(); ++i)
	{
		auto &addr=a[i];

		if (addr.address.empty())
			continue;

		// Addressbook lookup uses utf-8, format the address as such.

		std::string s;
		addr.address.print(
			std::back_inserter(s)
		);

		if (s.empty())
			continue;

		// If this address was seen, remove it, continue.
		if (!seen_addresses.insert(s).second)
		{
			a.erase(a.begin()+i);
			--i;
			continue;
		}

		if (!addr.name.empty())
		{
			seen_addresses.insert(s);
			continue;	/* Can't be a nickname */
		}

		std::string q=ab_find(s);

		if (q.empty())
			continue;

		// Stash away the replacement address string, because
		// rfc822::tokens reference it.
		auto &saved_str=*replacements.insert(q).first;

		// And the replacement tokens are referenced by the
		// replacement addresses.
		replacement_tokens.insert(replacement_tokens.end(),
					  rfc822::tokens{saved_str});
		auto &trepl=*--replacement_tokens.end();

		rfc822::addresses arepl{trepl};

		// Replace the original address with the replacement
		// addresses.
		a.erase(a.begin()+i);
		a.insert(a.begin()+i, arepl.begin(), arepl.end());
		--i;
	}

	std::string newbuf;
	a.encode_wrapped(unicode::utf_8, 70,
			 wrap_address_line{newbuf});

	maildir_writemsg(newdraftfd, header.data(), header.size());
	maildir_writemsgstr(newdraftfd, newbuf.c_str());
	maildir_writemsgstr(newdraftfd, "\n");
}
