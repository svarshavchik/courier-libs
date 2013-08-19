#include "config.h"
#include	"rematchstr.h"



ReMatchStr::~ReMatchStr()
{
}

int ReMatchStr::NextChar()
{
	return (*pos == 0 ? -1: (int)(unsigned char)*pos++);
}

int ReMatchStr::CurrentChar()
{
	return (*pos == 0 ? -1: (int)(unsigned char)*pos);
}

off_t ReMatchStr::GetCurrentPos()
{
	return (pos-str);
}

void	ReMatchStr::SetCurrentPos(off_t p)
{
	pos=str+p;
}
