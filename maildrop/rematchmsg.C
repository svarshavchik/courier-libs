#include	"config.h"
#include	"rematchmsg.h"
#include	"message.h"
#include	<ctype.h>


ReMatchMsg::ReMatchMsg(Message *m, int flag, int flag2)
	: msg(m), header_only(flag), mergelines(flag2), eof(0),
	lastc(0), end_headers(0)
{
	start=msg->tell();
}

ReMatchMsg::~ReMatchMsg()
{
}

int ReMatchMsg::CurrentChar()
{
	if (eof)	return (-1);
	return (msg->peek());
}

int ReMatchMsg::NextChar()
{
	if (eof)	return (-1);

int	c;

	for (;;)
	{
		c=msg->get_c();

		if (c < 0)
		{
			eof=1;
			if (lastc != '\n')
			{
				c='\n';
				lastc='\n';
			}
			return (c);
		}

		if (c == '\r')	continue;	// Eat carriage returns.

		if (c == '\n')
		{
		int	nextc=msg->peek();

			if (nextc == '\r' || nextc == '\n')
			{
				if (mergelines)
					end_headers=msg->tell();
				mergelines=0;
				if (header_only)	eof=1;
				return (c);
			}
			if (mergelines && isspace(nextc))	continue;
		}
		lastc=c;
		return (c);
	}
}

off_t ReMatchMsg::GetCurrentPos()
{
	return (msg->tell());
}

void ReMatchMsg::SetCurrentPos(off_t p)
{
	if (p < msg->tell())	eof=0;
	if ( p < start || p == 0)
	{
		msg->seek(start);
		lastc=0;
	}
	else
	{
		msg->seek(p-1);
		lastc=msg->get_c();
	}
	if (p < end_headers)	mergelines=1;
	if (!mergelines && header_only)	eof=1;
}
