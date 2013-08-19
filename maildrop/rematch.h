#ifndef	rematch_h
#define	rematch_h


#include	"config.h"
#include	<sys/types.h>
// #include	<unistd.h>

////////////////////////////////////////////////////////////////////////////
//
// ReMatch is an abstract class used in matching text against regular
// expression.  The matched text may come from a memory buffer, or a file.
// The matching logic does not care, and uses an abstracted data source,
// represented by this class, which supplies the text being matched,
// character by character.
//
// Also, in order to support the '!' operator, GetCurrentPos() and
// SetCurrentPos() functions must be implemented as a rudimentary "seek"
// mechanism.
//
////////////////////////////////////////////////////////////////////////////

class ReMatch {
public:
	ReMatch()	{}
	virtual ~ReMatch();

	virtual int NextChar()=0;
	virtual int CurrentChar()=0;
	virtual off_t GetCurrentPos()=0;
	virtual void SetCurrentPos(off_t)=0;
} ;
#endif
