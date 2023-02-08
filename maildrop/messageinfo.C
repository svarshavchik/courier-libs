#include "config.h"
#include	"messageinfo.h"
#include	"message.h"
#include	<ctype.h>

void MessageInfo::info(Message &msg)
{
Buffer	buf;

	msg.Rewind();
	fromname="MAILER-DAEMON";

	for (;;)
	{
		buf.clear();
		if (msg.appendline(buf) < 0)	return;

		auto	l=buf.size();

		const char *p=buf.c_str();

		if (l && p[l-1] == '\n')
		{
			--l;
			buf.resize(l);
		}

		if (l == 0)	break;

		p=buf.c_str();
		if (strncasecmp(p, "Return-Path:", 12))
			continue;

		p += 12;
		l -= 12;

		while (*p && *p != '\n' && isspace(*p) && l)
		{
			++p;
			--l;
		}

		if (l && *p == '<')
		{
			++p;
			--l;
		}

		size_t i;

		for (i=0; i<l; i++)
		{
			if (!p[i])
				break;
			if (p[i] == '>')
				break;
			if (isspace(p[i]))
				break;
		}
		fromname.clear();
		fromname.append(p, p+i);
		break;
	}
}
