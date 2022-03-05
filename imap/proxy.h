#ifndef	proxy_h
#define	proxy_h

#include <string>
#include <functional>

/*
** Copyright 2004 S. Varshavchik.
** See COPYING for distribution information.
*/

struct proxyinfo {
	const char *host;
	int port;

	std::function<int (int, const std::string &)> connected_func;
};

int connect_proxy(struct proxyinfo *);
void proxyloop(int);

struct proxybuf {
	char buffer[256];
	char *bufptr=nullptr;
	size_t bufleft=0;
};

int proxy_readline(int fd, struct proxybuf *pb,
		   char *linebuf,
		   size_t linebuflen,
		   int imapmode);
int proxy_write(int fd, const std::string &hostname,
		const char *buf, size_t buf_len);

#endif
