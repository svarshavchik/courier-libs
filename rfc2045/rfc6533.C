/*
** Copyright 2018 S. Varshavchik.  See COPYING for
** distribution information.
*/

/*
*/

#if    HAVE_CONFIG_H
#include "rfc2045_config.h"
#endif
#include	"rfc2045.h"
#include	<courier-unicode.h>
#include	<algorithm>
#include	<charconv>
#include	<idn2.h>

static const char xdigit[]="0123456789ABCDEF";

namespace {

	struct sink {
		virtual void operator()(std::string_view)=0;
	};

	struct count : sink {
		size_t n=0;

		void operator()(std::string_view str) override
		{
			n += str.size();
		}
	};

	struct save : sink {
		std::string s;
		void operator()(std::string_view str) override
		{
			s += str;
		}
	};
}

static void encode_rfc822_or_utf_8(std::string_view address,
				   sink &s,
				   rfc6533::format f)
{
	if (f == rfc6533::format::rfc822)
	{
		s("rfc822;");
	}
	else
	{
		s("utf-8;");
	}

	while (!address.empty())
	{
		size_t i;

		for (i=0; i<address.size(); ++i)
		{
			if (f != rfc6533::format::rfc822)
			{
				if (address[i] < '!' || address[i] == 127)
					break;
			}
			else
			{
				if (address[i] < '!' || address[i] > '~')
					break;
			}
			if (address[i] == '+' || address[i] == '=' ||
			    address[i] == '\\')
				break;
		}

		if (i == 0)
		{
			char h[2]={xdigit[((address[0] >> 4) & 15)],
				   xdigit[(address[0] & 15)]};

			if (f == rfc6533::format::rfc822)
			{
				s("+");
				s({h, 2});
			}
			else
			{
				s("\\x{");
				s({h, 2});
				s("}");
			}
			address.remove_prefix(1);
			continue;
		}

		s(address.substr(0, i));
		address.remove_prefix(i);
	}
}

static void encode_rfc6533(std::u32string_view address,
			   sink &s,
			   rfc6533::format f)
{
	std::string utf8buf;

	s("utf-8;");

	while (!address.empty())
	{
		size_t i;

		for (i=0; address[i]; ++i)
		{
			if (address[i] <= ' ')
				break;

			if (address[i] == '\\' ||
			    address[i] == '+' ||
			    address[i] == '=' ||
			    address[i] == 0x7F ||
			    (f == rfc6533::format::unitext &&
			     address[i] > 0x7f))
				break;
		}

		if (i == 0)
		{
			char buf[32];

			(s)("\\x{");

			auto e=std::to_chars(buf, buf+sizeof(buf), address[0],
					     16).ptr;
			for (char *b=buf; b != e; ++b)
				if (*b >= 'a' && *b <= 'f')
					*b -= 'a'-'A';

			s({buf, (size_t)(e-buf)});
			s("}");
			address.remove_prefix(1);
			continue;
		}

		utf8buf.clear();
		bool errflag;
		unicode::iconvert::fromu::convert(
			address.data(),
			address.data()+i,
			unicode::utf_8,
			std::back_inserter(utf8buf),
			errflag);

		s(utf8buf);
		address.remove_prefix(i);
	}
}

std::string rfc6533::encode(std::string_view address, format f)
{
	auto ret=unicode::iconvert::tou::convert(address, unicode::utf_8);

	return encode(std::get<0>(ret), f);
}

std::string rfc6533::encode(std::u32string_view address, format f)
{
	save s;

	if (f == format::rfc822
	    || f == format::xtext
	    || (f == format::utf_8 &&
		std::find_if(address.begin(), address.end(),
			     [&f]
			     (char32_t c)
			     {
				     return (c & 0x7F) != c;
			     }) == address.end()))
	{
		if (f == format::utf_8)
			f=format::rfc822;

		auto ret=unicode::iconvert::fromu::convert(
			address, unicode::utf_8
		);

		size_t at;

		if (f == format::rfc822 &&
		    (at=std::get<0>(ret).find_last_of('@'))
		    != std::get<0>(ret).npos)
		{
			char *p=0;

			++at;
			if (idna_to_ascii_8z(std::get<0>(ret).c_str()+at, &p, 0)
			    == IDNA_SUCCESS)
			{
				std::get<0>(ret).resize(at);

				std::get<0>(ret).reserve(at+strlen(p));
				std::get<0>(ret) += p;
			}

			if (p)
				free(p);
		}

		{
			count c;
			encode_rfc822_or_utf_8(std::get<0>(ret), c, f);
			s.s.reserve(c.n);
		}


		encode_rfc822_or_utf_8(std::get<0>(ret), s, f);
		return std::move(s.s);
	}


	{
		count c;
		encode_rfc6533(address, c, f);
		s.s.reserve(c.n);
	}

	encode_rfc6533(address, s, f);

	return std::move(s.s);
}

static int decode_rfc6533(std::string_view address,
			  sink &s)
{
	while (!address.empty())
	{
		auto i=address.find('\\');

		if (i != 0)
		{
			if (i == address.npos)
				i=address.size();

			s(address.substr(0, i));
			address.remove_prefix(i);
			continue;
		}

		if (address.substr(0, 3) != "\\x{")
			return -1;

		char32_t c=0;

		address.remove_prefix(3);

		while (!address.empty())
		{
			if (address[0] == '}')
				break;

			const char *p;

			p=strchr(xdigit, address[0]);
			if (!p)
				return -1;
			c <<= 4;
			c |= (p-xdigit);
			address.remove_prefix(1);
		}
		if (address.substr(0, 1) != "}")
			return -1;

		address.remove_prefix(1);
		if (c == 0)
			return -1;

		bool err=false;

		char buf[32];
		auto e=unicode::iconvert::fromu::convert(&c, &c+1,
							 unicode::utf_8,
							 (char *)buf,
							 err);

		s({buf, (size_t)(e-buf)});

	}
	return 0;
}

std::string rfc6533::decode(std::string_view address)
{
	save s;

	// Trim off spaces
	auto nspc=std::find_if(address.begin(), address.end(),
			       []
			       (unsigned char c)
			       {
				       return !unicode_isspace(c);
			       });

	address=address.substr(nspc-address.begin());

	size_t i;
	for (i=address.size(); i > 0; --i)
		if (!unicode_isspace((unsigned char)address[i]))
			break;

	address=address.substr(0, i);

	size_t semicolon=address.find(';');

	char typebuf[16];
	if (semicolon == address.npos || semicolon >= sizeof(typebuf)-1)
		return static_cast<std::string>(address);

	std::copy(address.data(), address.data()+semicolon, typebuf);

	std::transform(
		typebuf, typebuf+semicolon, typebuf,
		[]
		(char c)
		{
			if (c >= 'A' && c <= 'Z')
				c += 'a'-'A';
			return c;
		});

	std::string_view type{typebuf, semicolon};
	++semicolon;

	address.remove_prefix(semicolon);
	nspc=std::find_if(address.begin(), address.end(),
			  []
			  (unsigned char c)
			  {
				  return !unicode_isspace(c);
			  });

	address=address.substr(nspc-address.begin());

	if (type == "rfc822")
	{
		s.s.reserve(address.size());

		while (!address.empty())
		{
			if (address[0] != '+' || address.size() < 3)
			{
				s.s.push_back(address[0]);
				address.remove_prefix(1);
				continue;
			}

			address.remove_prefix(1);
			auto hi=strchr(xdigit, address[0]);
			address.remove_prefix(1);
			auto lo=strchr(xdigit, address[0]);
			address.remove_prefix(1);

			if (hi && lo)
			{
				char n= (char)
					((hi-xdigit) * 16
					 +(lo-xdigit));

				s.s.push_back(n);
			}
		}
	}
	else
	{
		if (type != "utf-8")
			return std::move(s.s);

		count c;

		if (decode_rfc6533(address, c))
			return std::move(s.s);

		s.s.reserve(c.n);
		decode_rfc6533(address, s);
	}

	if (std::find_if(s.s.begin(), s.s.end(),
			 []
			 (unsigned char c)
			 {
				 return c <= ' ';
			 }) != s.s.end())
		s.s.clear();

	size_t at=s.s.find_last_of('@');

	if (at != s.s.npos)
	{
		char *utf8;

		if (idn2_to_unicode_8z8z(s.s.c_str()+ ++at, &utf8, 0) !=
		    IDNA_SUCCESS)
		{
			utf8=0;
		}

		if (utf8)
		{
			s.s.resize(at);
			s.s += utf8;
			free(utf8);
		}
	}

	return std::move(s.s);
}
