#ifndef	rematchstr_h
#define	rematchstr_h


#include	"config.h"
#include	<sys/types.h>
#include	"rematch.h"

////////////////////////////////////////////////////////////////////////////
//
// ReMatchStr - derive from ReMatch when text matched against a regular
// expression comes from the message body itself.
//
////////////////////////////////////////////////////////////////////////////

class ReMatchStr : public ReMatch {

	const char *str;
	const char *pos;

public:
	ReMatchStr(const char *p) : str(p), pos(p)	{}
	~ReMatchStr();

	int	NextChar() override;
	int	CurrentChar() override;
	std::streampos GetCurrentPos() override;
	void	SetCurrentPos(std::streampos) override;
} ;
#endif
