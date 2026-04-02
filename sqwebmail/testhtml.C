#include "html.h"
#include "maildir.h"

#include <string.h>
#include <iostream>

static void write_stdout(const char32_t *uc, size_t n, void *dummy)
{
	while (n)
	{
		std::cout << (char)static_cast<unsigned char>(*uc++);
		--n;
	}
}

static std::string cid_func(const char *cid)
{
	return std::string{"cid:"} + cid;
}

int main(int argc, char **argv)
{
	struct htmlfilter_info *p;
	std::string buf;
	std::u32string ubuf;

	p=htmlfilter_alloc(write_stdout, NULL);

	htmlfilter_set_http_prefix(p, "http://redirect?");
	htmlfilter_set_mailto_prefix(p, "http://mailto?");
	htmlfilter_set_convertcid(p, cid_func);

	while (std::getline(std::cin, buf))
	{
		buf += "\n";
		ubuf.assign(buf.begin(), buf.end());

		htmlfilter(p, ubuf.data(), ubuf.size());
	}
	htmlfilter_free(p);
	return 0;
}
