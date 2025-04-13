#ifndef imapflags_h
#define imapflags_h

/*
** Copyright 2025 Sam Varshavchik
** See COPYING for distribution information.
*/

// IMAP flags


struct imapflags {
	bool	seen=false;
	bool	answered=false;
	bool	deleted=false;
	bool	flagged=false;
	bool	drafts=false;
	bool	recent=false; // Synthesized, not modified by += and -=

	imapflags &operator+=(const imapflags &other)
	{
		if (other.drafts)	drafts=true;
		if (other.seen)		seen=true;
		if (other.answered)	answered=true;
		if (other.deleted)	deleted=true;
		if (other.flagged)	flagged=true;
		return *this;
	}

	imapflags &operator-=(const imapflags &other)
	{
		if (other.drafts)	drafts=false;
		if (other.seen)		seen=false;
		if (other.answered)	answered=false;
		if (other.deleted)	deleted=false;
		if (other.flagged)	flagged=false;

		return *this;
	}
};

#endif
