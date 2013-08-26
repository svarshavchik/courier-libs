#include "config.h"
#include	"messageinfo.h"
#include	"message.h"
#include	<ctype.h>

void MessageInfo::info(Message &msg)
{
Buffer	buf;

	msg.Rewind();
	fromname.set("MAILER-DAEMON");

	for (;;)
	{
		buf.reset();
		if (msg.appendline(buf) < 0)	return;

		int	l=buf.Length();

		const char *p=buf;

		if (l && p[l-1] == '\n')
		{
			--l;
			buf.Length(l);
		}

		if (l == 0)	break;

		p=buf;
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

		int i;

		for (i=0; i<l; i++)
		{
			if (!p[i])
				break;
			if (p[i] == '>')
				break;
			if (isspace(p[i]))
				break;
		}
		fromname.reset();
		fromname.append(p, i);
		break;
	}
}
