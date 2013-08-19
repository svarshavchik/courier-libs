#include "config.h"
#include	"messageinfo.h"
#include	"message.h"


void MessageInfo::info(Message &msg)
{
Buffer	buf;
off_t	msgstart=0;

	msgoffset=0;
	msg.Rewind();
	fromname.reset();
	for (;;)
	{
		msgstart=msg.tell();
		buf.reset();
		if (msg.appendline(buf) < 0)	return;

	int	l=buf.Length();
	const	char *p=buf;

		if (l && p[l-1] == '\n')
		{
			--l;
			buf.Length(l);
		}

		if (l == 0)	continue;

		if (l < 5)	break;
		if (p[0] == 'F' && p[1] == 'r' && p[2] == 'o' && p[3] == 'm'
			&& p[4] == ' ')
		{
		int i;
			for (i=5; i<l; i++)
				if (p[i] == ' ')	break;

			fromname.reset();
			fromname.append(p+5, i-5);
		}
		else	break;
	}
	msgoffset=msgstart;
}
