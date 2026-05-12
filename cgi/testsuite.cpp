#include	"cgi.h"
#include	<iostream>
#include	<stdlib.h>
#include	<string>
#include	<sstream>
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

	static const char testparams[]="param1=value1&param1=value2+space&param2=val%3A1";

	setenv("REQUEST_METHOD", "GET", 1);
	setenv("QUERY_STRING", testparams, 1);
	cgi_setup();

	if (cgi_arglist !=
		std::unordered_map<std::string,
		std::vector<std::string>>{{"param1", {"value1", "value2 space"}},
		{"param2", {"val:1"}}}
		)
	{
		std::cerr << "cgi_setup (GET) failed\n";
		exit(1);
	}

	cgi_cleanup();

	if (!cgi_arglist.empty())
	{
		std::cerr << "cgi_cleanup failed\n";
		exit(1);
	}

	setenv("REQUEST_METHOD", "POST", 1);
	setenv("CONTENT_TYPE", "application/x-www-form-urlencoded", 1);

	{
		std::stringstream ss;

		ss << (sizeof(testparams)-1);
		setenv("CONTENT_LENGTH", ss.str().c_str(), 1);
	}

	{
		FILE *fp=tmpfile();

		fwrite(testparams, sizeof(testparams)-1, 1, fp);
		fseek(fp, 0, SEEK_SET);
		dup2(fileno(fp), 0);
		fclose(fp);
	}

	cgi_setup();

	if (cgi_arglist !=
		std::unordered_map<std::string,
		std::vector<std::string>>{
			{"param1", {"value1", "value2 space"}},
			{"param2", {"val:1"}}}
		)
	{
		std::cerr << "cgi_setup (POST) failed\n";
		exit(1);
	}

	cgi_cleanup();
	setenv("CONTENT_TYPE", "multipart/form-data; boundary=abc", 1);

	{
		static const char testparams[]=
			"--abc\r\n"
			"Content-Disposition: form-data; name=\"param1\"\r\n"
			"\r\n"
			"value1\r\n"
			"--abc\r\n"
			"Content-Disposition: form-data; name=\"param1\"\r\n"
			"\r\n"
			"value2 space\r\n"
			"--abc\r\n"
			"Content-Disposition: form-data; name=\"file\";"
				" filename=\"foo\"\r\n"
			"\r\n"
			"Hello world!\n"
			"Hello world!\n"
			"Hello world!"
			"\r\n"
			"--abc\r\n"
			"Content-Disposition: form-data; name=\"param2\"\r\n"
			"\r\n"
			"val%3A1\r\n"
			"--abc--\r\n"
		;
		std::stringstream ss;

		ss << (sizeof(testparams)-1);
		setenv("CONTENT_LENGTH", ss.str().c_str(), 1);

		FILE *fp=tmpfile();

		fwrite(testparams, sizeof(testparams)-1, 1, fp);
		fseek(fp, 0, SEEK_SET);
		dup2(fileno(fp), 0);
		fclose(fp);
	}
	cgi_setup();

	if (cgi_arglist !=
		std::unordered_map<std::string,
		std::vector<std::string>>{
			{"param1", {"value1", "value2 space"}},
			{"param2", {"val:1"}}}
		)
	{
		std::cerr << "cgi_setup (form-data) failed\n";
		exit(1);
	}

	std::string filecontents;

	if (cgi_getfiles(
		[](const char *name, const char *filename, void *vp){
			if (strcmp(name, "file") ||
			    strcmp(filename, "foo")
			)
			{
				return -1;
			}

			return 0;
		},
		[](const char *p, size_t n, void *vp) -> int {
			reinterpret_cast<std::string*>(vp)->append(p, n);
			return 0;
		},
		[](void *vp) {
			reinterpret_cast<std::string*>(vp)->append("<EOF>", 5);
		},
		0,
		&filecontents
	) != 0)
	{
		std::cerr << "cgi_getfiles(1) failed\n";
		exit(1);
	}

	if (filecontents != "Hello world!\nHello world!\nHello world!<EOF>")
	{
		std::cerr << "cgi_getfiles(1) contents invalid\n";
		exit(1);
	}
	cgi_cleanup();

	std::string nulls;

	nulls.assign(4096, (char)'\0');

	{
		std::stringstream content_ss;

		content_ss <<
			"--abc\r\n"
			"Content-Disposition: form-data; name=\"file\";"
				" filename=\"foo\"\r\n"
			"\r\n"
		   << nulls
		   << "\r\n--abc--\r\n";

		auto content=content_ss.str();

		std::stringstream ss;
		ss << content.size();
		setenv("CONTENT_LENGTH", ss.str().c_str(), 1);

		FILE *fp=tmpfile();

		fwrite(content.c_str(), content.size(), 1, fp);
		fseek(fp, 0, SEEK_SET);
		dup2(fileno(fp), 0);
		fclose(fp);
	}
	cgi_setup();

	filecontents.clear();
	if (cgi_getfiles(
		[](const char *name, const char *filename, void *vp){
			return 0;
		},
		[](const char *p, size_t n, void *vp) -> int {
			reinterpret_cast<std::string*>(vp)->append(p, n);
			return 0;
		},
		[](void *vp) {
		},
		0,
		&filecontents
	    ) != 0)
	{
		std::cerr << "cgi_getfiles(2) failed\n";
		exit(1);
	}

	if (filecontents != nulls)
	{
		std::cerr << "cgi_getfiles(1) contents invalid\n";
		exit(1);
	}

	setenv("HTTP_COOKIE", "cookie1=value1; cookie2=\"value2\";"
		" cookie3=value3", 1);

	if (cgi_get_cookie("cookie1") != "value1")
	{
		std::cerr << "cgi_get_cookie(1) failed\n";
		exit(1);
	}
	if (cgi_get_cookie("cookie2") != "value2")
	{
		std::cerr << "cgi_get_cookie(2) failed\n";
		exit(1);
	}
	if (cgi_get_cookie("cookie3") != "value3")
	{
		std::cerr << "cgi_get_cookie(3) failed\n";
		exit(1);
	}
	return 0;
}

void fake_exit(int n)
{
	exit(n);
}
