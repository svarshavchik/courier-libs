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

std::streampos ReMatchStr::GetCurrentPos()
{
	return (pos-str);
}

void	ReMatchStr::SetCurrentPos(std::streampos p)
{
	pos=str+p;
}
