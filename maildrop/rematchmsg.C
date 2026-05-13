#include	"config.h"
#include	"rematchmsg.h"
#include	"message.h"
#include	<ctype.h>


ReMatchMsg::ReMatchMsg(Message *m, int flag, int flag2)
	: msg(m), header_only(flag), mergelines(flag2), eof(0),
	lastc(0), end_headers(0)
{
	start=msg->tellg();
}

ReMatchMsg::~ReMatchMsg()
{
}

int ReMatchMsg::CurrentChar()
{
	if (eof)	return (-1);
	return (msg->sgetc());
}

int ReMatchMsg::NextChar()
{
	if (eof)	return (-1);

int	c;

	for (;;)
	{
		c=msg->sbumpc();

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
		int	nextc=msg->sgetc();

			if (nextc == '\r' || nextc == '\n')
			{
				if (mergelines)
					end_headers=msg->tellg();
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

std::streampos ReMatchMsg::GetCurrentPos()
{
	return (msg->tellg());
}

void ReMatchMsg::SetCurrentPos(std::streampos p)
{
	if (p < msg->tellg())	eof=0;
	if ( p < start || p == 0)
	{
		msg->pubseekpos(start);
		lastc=0;
	}
	else
	{
		msg->pubseekpos(p-std::streamoff{1});
		lastc=msg->sbumpc();
	}
	if (p < end_headers)	mergelines=1;
	if (!mergelines && header_only)	eof=1;
}
