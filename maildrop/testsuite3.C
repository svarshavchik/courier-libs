#include "config.h"
#include "message.h"
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <filesystem>

void testsmallmessage(Maildrop &md)
{
	Message m{md};
	FILE *fp=tmpfile();

	fprintf(fp, "Subject: small message\n"
		"\n"
		"Hello world.\n");
	fseek(fp, 0L, SEEK_SET);
	m.Init(fileno(fp), "From: nobody@example.com\n");
	fclose(fp);

	std::stringstream ss;

	ss << &m;

	if (ss.str() !=
	    "From: nobody@example.com\n"
	    "Subject: small message\n"
	    "\n"
	    "Hello world.\n")
	{
		std::cerr << "testsmallmessage: readback incorrect (1):\n"
			  << ss.str();
		exit(1);
	}

	ss.str("");
	m.pubseekoff(6, std::ios::beg);
	if (m.tellg() != 6)
	{
		std::cerr << "tellg incorrect, expected 6, got: "
			  << m.tellg() << "\n";
		exit(1);
	}
	ss << &m;
	if (ss.str() !=
	    "nobody@example.com\n"
	    "Subject: small message\n"
	    "\n"
	    "Hello world.\n")
	{
		std::cerr << "testsmallmessage: readback incorrect (2):\n"
			  << ss.str();
		exit(1);
	}

	m.pubseekoff(6, std::ios::beg);
	m.pubseekoff(-6, std::ios::cur);
	ss.str("");
	ss << &m;
	if (ss.str() !=
	    "From: nobody@example.com\n"
	    "Subject: small message\n"
	    "\n"
	    "Hello world.\n")
	{
		std::cerr << "testsmallmessage: readback incorrect (3):\n"
			  << ss.str();
		exit(1);
	}

	m.pubseekoff(-7, std::ios::cur);
	ss.str("");
	ss << &m;

	if (ss.str() != "world.\n")
	{
		std::cerr << "testsmallmessage: readback incorrect (4):\n"
			  << ss.str();
		exit(1);
	}
}

std::string long_message()
{
	std::string s(79, 'A');

	s += "\n";

	s += s; // 160
	s += s; // 320
	s += s; // 640
	s += s; // 1280
	s += s; // 2560
	s += s; // 5120
	s += s; // 10240

	return "Subject: long message\n\n" + s;
}

void longmessagetest(Maildrop &md, int fd, const char *file_type)
{
	Message m{md};

	std::string full_message="From: nobody@example.com\n";
	m.Init(fd, full_message);

	full_message += long_message();

	std::stringstream ss;

	ss << &m;

	if (ss.str() != full_message)
	{
		std::cerr << "testlongmessage(" << file_type
			  << "): readback incorrect (1):\n"
			  << ss.str().substr(0,100) << "...\n";
		exit(1);
	}

	ss.str("");
	m.pubseekoff(6, std::ios::beg);
	if (m.tellg() != 6)
	{
		std::cerr << "tellg incorrect, expected 6, got: "
			  << m.tellg() << "\n";
		exit(1);
	}
	ss << &m;
	if (ss.str() != full_message.substr(6))
	{
		std::cerr << "testlongmessage("
			  << file_type << "): readback incorrect (2):\n"
			  << ss.str();
		exit(1);
	}

	m.pubseekoff(6, std::ios::beg);
	m.pubseekoff(-6, std::ios::cur);
	ss.str("");
	ss << &m;
	if (ss.str() != full_message)
	{
		std::cerr << "testlongmessage("
			  << file_type << "): readback incorrect (3):\n"
			  << ss.str();
		exit(1);
	}

	m.pubseekoff(-7, std::ios::cur);
	ss.str("");
	ss << &m;

	if (ss.str() != "AAAAAA\n")
	{
		std::cerr << "testlongmessage("
			  << file_type << "): readback incorrect (4):\n"
			  << ss.str();
		exit(1);
	}
}

int main()
{
	Maildrop md;

#if	SHARED_TEMPDIR

#else
	std::error_code ec;
	md.tempdir=".tmpdir";
	std::filesystem::create_directory(md.tempdir, ec);
#endif
	testsmallmessage(md);

	FILE *f=tmpfile();
	{
		auto m=long_message();
		fprintf(f, "%s", m.c_str());
	}

	fseek(f, 0L, SEEK_SET);
	longmessagetest(md, fileno(f), "seekable");
	fclose(f);

	int pfd[2];
	if (pipe(pfd) < 0)
	{
		perror("pipe");
		exit(1);
	}

	if (fork() == 0)
	{
		close(pfd[0]);
		auto m=long_message();
		char *p=m.data();
		size_t s=m.size();

		while (s)
		{
			auto n=write(pfd[1], p, s);

			if (n <= 0)
				break;
			p += n;
			s -= n;
		}
		close(pfd[1]);
		return 0;
	}

	close(pfd[1]);
	longmessagetest(md, pfd[0], "pipe");
	close(pfd[0]);
#if	SHARED_TEMPDIR

#else
	std::filesystem::remove_all(md.tempdir, ec);
#endif
	return 0;
}
