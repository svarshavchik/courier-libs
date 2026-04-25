#include	"cgi.h"
#include	<iostream>
#include	<stdlib.h>
#include	<algorithm>
#include	<type_traits>

int main()
{
	static const char parameter[]="rolem=&:ipsum\x09\x80";

	std::string as;
	std::string bs;
	std::string cs;

	if (cgi_encode::estimate(parameter) != 25)
	{
		std::cerr << "estimate failed\n";
		exit(1);
	}

	if (cgi_encode::estimate(parameter, cgi_encode::noamp) != 23)
	{
		std::cerr << "estimate (noamp) failed\n";
		exit(1);
	}

	if (cgi_encode::estimate(parameter, cgi_encode::noeq) != 23)
	{
		std::cerr << "estimate failed\n";
		exit(1);
	}

	if (cgi_encode::estimate(parameter) != 25)
	{
		std::cerr << "estimate failed\n";
		exit(1);
	}

	cgi_encode::encode( std::back_inserter(as), parameter);
	cgi_encode::encode( std::back_inserter(bs), parameter,
			    cgi_encode::noamp);
	cgi_encode::encode( std::back_inserter(cs), parameter,
			    cgi_encode::noeq);

	static_assert(
		std::is_same_v<
		decltype(std::back_inserter(cs)),
		decltype(cgi_encode::encode(
				 std::back_inserter(as),
				 parameter))
		>,
		"Passed in iterator by value gets returned from encode()"
	);

	static_assert(
		std::is_same_v<
		void,
		decltype(cgi_encode::encode(
				 std::declval<decltype(std::back_inserter(cs))
				 &>(),
				 parameter))
		>,
		"encode() returns void if oassed in iterator by ref"
	);

	if (as != "rolem%3D%26%3Aipsum%09%80")
	{
		std::cerr << "encode failed\n";
		exit(1);
	}
	if (bs != "rolem%3D&%3Aipsum%09%80")
	{
		std::cerr << "encode (noamp) failed\n";
		exit(1);
	}
	if (cs != "rolem=%26%3Aipsum%09%80")
	{
		std::cerr << "encode (noeq) failed\n";
		exit(1);
	}

	return 0;
}
