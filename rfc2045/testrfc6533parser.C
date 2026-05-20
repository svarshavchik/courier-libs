/*
** Copyright 2018-2026 S. Varshavchik.
** See COPYING for distribution information.
*/

#include	"rfc2045.h"

#include	<iostream>

static const struct {
	const char *address;
	rfc6533::format f;
	const char *result;
} encode_tests[]={
		  {"nobody@example.com",
		   rfc6533::format::rfc822, "rfc822;nobody@example.com"},
		  {"nobody@example.com",
		   rfc6533::format::utf_8, "rfc822;nobody@example.com"},
		  {"nobody+=me@example.com",
		   rfc6533::format::rfc822,
		   "rfc822;nobody+2B+3Dme@example.com"},
		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5@example.com",
		   rfc6533::format::unitext,
		   "utf-8;\\x{438}\\x{441}\\x{43F}\\x{44B}\\x{442}\\x{430}\\x{43D}\\x{438}\\x{435}@example.com"},
		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5@example.com",
		   rfc6533::format::utf_8,
		   "utf-8;\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5@example.com"},
		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5\\+=@example.com",
		   rfc6533::format::utf_8,
		   "utf-8;\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5\\x{5C}\\x{2B}\\x{3D}@example.com"},
		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5@example.com",
		   rfc6533::format::rfc822, "rfc822;+D0+B8+D1+81+D0+BF+D1+8B+D1+82+D0+B0+D0+BD+D0+B8+D0+B5@example.com"},
		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5+=\\me@example.com",
		   rfc6533::format::unitext,
		   "utf-8;\\x{438}\\x{441}\\x{43F}\\x{44B}\\x{442}\\x{430}\\x{43D}\\x{438}\\x{435}\\x{2B}\\x{3D}\\x{5C}me@example.com"},


		  {"\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5@\xd0\xb8\xd1\x81\xd0\xbf\xd1\x8b\xd1\x82\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5.net",
		   rfc6533::format::rfc822,
		   "rfc822;+D0+B8+D1+81+D0+BF+D1+8B+D1+82+D0+B0+D0+BD+D0+B8+D0+B5@xn--80akhbyknj4f.net"
		  },
};

int main(int argc, char **argv)
{
	for (auto &t:encode_tests)
	{
		auto p=rfc6533::encode(t.address, t.f);

		if (p != t.result)
		{
			std::cerr << "Expected to encode "
				  << t.address << " as "
				  << t.result << ", got " << p << "\n";
			exit(1);
		}
		auto q=rfc6533::decode(p);

		if (q.empty())
		{
			std::cerr << "Could not decode " << p << "\n";
			exit(1);
		}

		if (q != t.address)
		{
			std::cerr << "Expected to decode "
				  << p << " as "
				  << t.address << ", got "
				  << q << "\n";
			exit(1);
		}
	}

	exit(0);
}
