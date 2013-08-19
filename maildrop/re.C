#include	"config.h"
#include	"re.h"
#include	"mio.h"
#include	"regexpnode.h"
#include	"rematch.h"
#include	"funcs.h"
#include	"buffer.h"
#include	<ctype.h>


//////////////////////////////////////////////////////////////////////////////
//
// Create sets for the [:is....:] codes.
//

static void mk_alnum(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isalnum(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_alpha(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isalpha(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_cntrl(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (iscntrl(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_digit(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isdigit(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_graph(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isgraph(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_lower(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (islower(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_print(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isprint(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_punct(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (ispunct(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_space(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isspace(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_upper(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isupper(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_xdigit(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (isxdigit(i))
			p[i/8] |= 1 << (i % 8);
}

static void mk_wbreak(unsigned char *p)
{
register unsigned i;

	for (i=0; i<256; i++)
		if (!isalnum(i) && i != '_')
			p[i/8] |= 1 << (i % 8);
}

static const char *const is_setname[]={
	":alnum:",
	":alpha:",
	":cntrl:",
	":digit:",
	":graph:",
	":lower:",
	":print:",
	":punct:",
	":space:",
	":upper:",
	":xdigit:",
	":wbreak:"};

static void (*is_setfunc[])(unsigned char *)={
	mk_alnum,
	mk_alpha,
	mk_cntrl,
	mk_digit,
	mk_graph,
	mk_lower,
	mk_print,
	mk_punct,
	mk_space,
	mk_upper,
	mk_xdigit,
	mk_wbreak};

Re::Re() : chainedre(0), prevre(0), nodes(0), first(0), isCaret(0)
{
}

Re::~Re()
{
	init();
}

void Re::init()
{
	if (chainedre)	delete chainedre;
	chainedre=0;

RegExpNode *n;

	while ((n=nodes) != 0)
	{
		nodes=n->next;
		delete n;
	}
}

inline RegExpNode	*Re::allocnode()
{
RegExpNode	*n;

	if ((n=new RegExpNode(nextid++)) == 0)
		outofmem();
	n->next=nodes; nodes=n; return(n);
}

int Re::Compile(const char *ptr, int caseflag, int &errindex)
{
	if (*ptr == '^')
	{
		if (CompileS(ptr+1, caseflag, errindex))	return (-1);
		isCaret=1;
		return (0);
	}

	if (CompileS("[.\n]*", 1, errindex) < 0)	return (-1);
	if ((chainedre=new Re) == 0)
		outofmem();
	isDummy=1;
	chainedre->prevre=this;
	return (chainedre->CompileS(ptr, caseflag, errindex));
}

int Re::CompileS(const char *ptr, int caseflag, int &errindex)
{
	expr=ptr;
	origexpr=expr;
	init();
	nextid=0;
	first=0;
	isCaret=0;
	isDummy=0;
	casesensitive=caseflag;
	matchFull=0;

int	rc=0;

	try
	{
	RegExpNode **p=CompileOrClause(&first);

		if (*expr == '!')
		{
		int dummy;

			++expr;
			if ((chainedre=new Re) == 0)
				outofmem();
			if ( chainedre->CompileS(expr, caseflag, dummy) < 0)
			{
				expr += dummy;
				throw -1;
			}
			chainedre->prevre=this;
			if (VerboseLevel() > 7)
				merr.write("\n*** CHAINED TO ***\n");

		} else if (curchar())	throw -1;

		final=*p=allocnode();
		final->thechar=REFINAL;
	}
	catch (int n)
	{
		init();
		errindex=expr-origexpr;
		rc= n;
	}
	if (rc == 0 && VerboseLevel() > 7)
	{
	RegExpNode *n;
	Buffer	b;

		if (first)
		{
			b="Start node: ";
			b.append( (unsigned long)first->id );
			b += "\n\n";
			b += '\0';
			merr.write(b);
		}
		for (n=nodes; n; n=n->next)
		{
			b="Node ";
			b.append( (unsigned long)n->id );
			b += ": ";
			switch (n->thechar)	{
			case RENULL:
				b  += "null";
				break;
			case RESET:
				b += "[set] ";
				{
				int i,j=0;

					for (i=0; i<256; i=j)
					{
						j=i+1;
						if ((n->reset[i/8] &
							(1 << (i % 8))) == 0)
							continue;
						for (j=i; j<256; j++)
							if ((n->reset[j/8] &
								(1 << (j % 8)))
								== 0)
							break;
						if (i < ' ' || i > 127)
						{
							b += '#';
							b.append((unsigned long)
								i);
						}
						else
						{
							if (i == '#'
								|| i == '-'
								|| i == '\\')
								b += '\\';
							b += (char)i;
						}
						if (i+1 == j)	continue;
						b += ('-');
						--j;
					}
				}
				break;
			case REFINAL:
				b += "final";
				break;
			default:
				if (n->thechar >= ' ' && n->thechar < 127)
				{
					b += '\'';
					b += (char)n->thechar;
					b += '\'';
				}
				else
				{
					b += "chr(";
					b.append((unsigned long)n->thechar);
					b += ')';
				}
			}
			b += '\n';
			b += '\0';
			merr.write( b );
			if (n->next1)
			{
				b="    transition to ";
				b.append((unsigned long)n->next1->id);
				b += '\n';
				b += '\0';
				merr.write(b);
			}

			if (n->next2)
			{
				b="    transition to ";
				b.append((unsigned long)n->next2->id);
				b += '\n';
				b += '\0';
				merr.write(b);
			}
			merr.write("\n");
		}
	}
	return (rc);
}

RegExpNode **Re::CompileOrClause(RegExpNode **ptr)
{
RegExpNode **finish=CompileAtomString(ptr);

	if ( curchar() != '|')	return (finish);

RegExpNode *realfinish=allocnode();

	realfinish->thechar=RENULL;
	*finish=realfinish;

	while ( curchar() == '|' )
	{
		nextchar();

	RegExpNode *newstart=allocnode();

		newstart->thechar=RENULL;
		newstart->next1= *ptr;
		*ptr=newstart;

		finish=CompileAtomString(&newstart->next2);
		*finish=realfinish;
	}
	return (&realfinish->next1);
}

RegExpNode **Re::CompileAtomString(RegExpNode **ptr)
{
int	c;

	for (;;)
	{
		c=curchar();
		if (c == 0 || c == '|' || c == ')' || c == '!')
			break;
		ptr=CompileElement(ptr);
	}
	return (ptr);
}

RegExpNode **Re::CompileElement(RegExpNode **start)
{
RegExpNode **finish;

	if (curchar() != '$')
	{
		finish=CompileAtom(start);
	}
	else
	{
		nextchar();
		if (curchar() == 0)
		{
			matchFull=1;
			return (start);
		}
		(*start)=allocnode();
		(*start)->thechar='$';
		finish= & (*start)->next1;
	}

	switch (curchar())	{
	case '+':
		(*finish)=allocnode();
		(*finish)->thechar=RENULL;
		(*finish)->next1=(*start);
		finish= &(*finish)->next2;
		nextchar();
		break;
	case '*':
		(*finish)=allocnode();
		(*finish)->thechar=RENULL;
		(*finish)->next1=(*start);
		(*start)=(*finish);
		finish= &(*finish)->next2;
		nextchar();
		break;
	case '?':

		{
		RegExpNode *newstart=allocnode();

			newstart->thechar=RENULL;
			(*finish)=allocnode();
			(*finish)->thechar=RENULL;
			newstart->next1= *start;
			newstart->next2= *finish;
			*start=newstart;
			finish= &(*finish)->next1;
			nextchar();
		}
		break;
	}
	return (finish);
}

RegExpNode **Re::CompileAtom(RegExpNode **ptr)
{
int	c=curchar();

	if (c == '(')	// Subexpression
	{
		nextchar();
		ptr=CompileOrClause(ptr);
		if ( curchar() != ')')	throw -1;
		nextchar();
		return (ptr);
	}

	(*ptr)=allocnode();

	if (c == '[' || c == '.')
	{
	int	i, complement=0;

		if ( ((*ptr)->reset=new unsigned char[256/8]) == 0)
			outofmem();
		for (i=0; i<256/8; i++)
			(*ptr)->reset[i]=0;

		if ( c == '.' )
		{
			(*ptr)->reset[ '\n' / 8 ] |= 1 << ('\n' % 8);
			complement=1;
		}
		else
		{
			nextchar();
			if ( curchar() == '^')
			{
				complement=1;
				nextchar();
			}

			is_sets(*ptr);
		}
		nextchar();
		if (complement)
			for (i=0; i<256/8; i++)
				(*ptr)->reset[i] ^= ~0;
		c=RESET;
	}
	else c=parsechar();

	(*ptr)->thechar=c;
	return (&(*ptr)->next1);
}

void Re::is_sets(RegExpNode *p)
{
Buffer	buf;
int	c=curchar();
int	call_parsechar=1;

	if (c == ':')
	{
		do
		{
			buf += c;
			nextchar();
		} while ( (c=curchar()) >= 0 && isalpha(c));

		if (c == ':')
		{
			buf += c;
			nextchar();
			c=curchar();
			if (c == ']')
			{
				buf += '\0';

			const char *q=(const char *)buf;
			unsigned i;

				for (i=0; i<sizeof(is_setname)/
					sizeof(is_setname[0]); i++)
				{
					if (strcmp(is_setname[i], q) == 0)
					{
						(*is_setfunc[i])(p->reset);
						return;
					}
				}
			}
		}

	int	i=0;

		for (i=0; i<buf.Length(); i++)
		{
			c=(int)(unsigned char)((const char*)buf)[i];
			p->reset[ c / 8 ] |= 1 << (c % 8);
		}
		// In case the next character is '-', leave 'c' the way it
		// is.
		call_parsechar=0;
		if (curchar() == ']')	return;
	}

	do
	{
	int	c2;

		if (c == 0)	throw -1;

		if (c == '.')
		{
			for (c2=0; c2 < 256/8; c2++)
				if (c2 != '\n' / 8)
					p->reset[c2]= ~0;
				else
					p->reset[c2] |= ~(1 << ('\n' % 8));
			if (call_parsechar)
				nextchar();
			call_parsechar=1;
			continue;
		}
		if (call_parsechar)
			c=parsechar();
		c2=c;
		call_parsechar=1;

		if (curchar() == '-')
		{
			nextchar();
			c2=parsechar();
		}
		while ( c <= c2 )
		{
			p->reset[ c / 8 ] |= 1 << (c % 8);
			++c;
		}
	} while ((c=curchar()) != ']');
}

int Re::parsechar()
{
int	c;

	c=curchar();
	if (c == 0)	throw -1;
	nextchar();
	if (c != '\\') return (c);
	c=curchar();

	if (c == 0)
		throw -1;
	else if (c >= '0' && c <= '7')
	{
	unsigned char uc=0;

		while ( c >= '0' && c <= '7' )
		{
			uc = uc * 8 + (c-'0');
			nextchar();
			c=curchar();
		}
		c=uc;
	}
	else
	{
		c=backslash_char(c);
		nextchar();
	}
	return (c);
}

/////////////////////////////////////////////////////////////////////////////

int Re::Match(ReMatch &string)
{
	matched= -1;
	matchedpos=0;

	charsmatched=0;
	state1.init(nextid);
	state2.init(nextid);

	curstate= &state1;
	nextstate= &state2;

	curstate->nodes[0]=first;
	curstate->numnodes=1;
	curstate->nodenums[first->id]=0;

	final_id=final->id;

	if (VerboseLevel() > 8)
	{
		merr.write("*** MATCH START ***\n");
	}

	for (;;)
	{
	// Compute null closure

	unsigned n;

		for (n=0; n<curstate->numnodes; n++)
		{
		RegExpNode *p=curstate->nodes[n];

			if (p->thechar != RENULL)	continue;

		RegExpNode *q=p->next1;

			if (q && curstate->nodenums[q->id] != charsmatched)
			{
				curstate->nodes[curstate->numnodes++]=q;
				curstate->nodenums[q->id]=charsmatched;
				if (VerboseLevel() > 8)
				{
				Buffer b;

					b="  Transition to state ";
					b.append((unsigned long)q->id);
					b += '\n';
					b += '\0';
					merr.write(b);
				}
			}

			q=p->next2;
			if (q && curstate->nodenums[q->id] != charsmatched)
			{
				curstate->nodes[curstate->numnodes++]=q;
				curstate->nodenums[q->id]=charsmatched;
				if (VerboseLevel() > 8)
				{
				Buffer b;

					b="  Transition to state ";
					b.append((unsigned long)q->id);
					b += '\n';
					b += '\0';
					merr.write(b);
				}
			}
		}

	int	nextChar;

		if (curstate->nodenums[final_id] == charsmatched)
		{
		off_t	pos=string.GetCurrentPos();

			if (VerboseLevel() > 8)
				merr.write("**Final node.\n");
			if (chainedre)
			{
			unsigned long saved_matched_chainedre=
					chainedre->charsmatched;

				// On subsequent passes, charsmatched gets
				// reset.  If, previously, we had a match,
				// don't forget # of characters matched!

				if (VerboseLevel() > 8)
					merr.write(
					"**Final node - checking subexpr.\n");
				if (chainedre->Match(string) == 0)
				{
					if (VerboseLevel() > 8)
					{
					Buffer	buf;

						buf="**Subexpr matched after ";
						buf.append( (unsigned long)
							charsmatched);
						buf += " characters.\n";
						buf += '\0';
						merr.write(buf);
					}
					matched=0;
					matchedpos=charsmatched;
					if (isDummy)	// Don't need to
							// look for max matches
							// for the dummy block
					{
						return (0);
					}
				}
				else
				{
					if (VerboseLevel() > 8)
						merr.write(
						"**Subexpr didn't match.\n");
					chainedre->charsmatched=
						saved_matched_chainedre;
				}
				string.SetCurrentPos(pos);
				nextChar=string.NextChar();
			}
			else
			{
				if (!matchFull)	// We don't need to match full
						// string.
				{
					if (VerboseLevel() > 8)
					{
					Buffer	buf;

						buf="Matched ";
						buf.append( (unsigned long)
							charsmatched);
						buf += " characters.\n";
						buf += '\0';
						merr.write(buf);
					}
					matched=0;
					matchedpos=charsmatched;
				}

				nextChar=string.NextChar();
				if ( nextChar < 0)
				{
					if (VerboseLevel() > 8)
					{
					Buffer	buf;

						buf="Matched ";
						buf.append( (unsigned long)
							charsmatched);
						buf += " characters.\n";
						buf += '\0';
						merr.write(buf);
					}
					return (0);	// Matched everything
				}
			}
		}
		else nextChar=string.NextChar();

		if (nextChar < 0)
		{
			if (VerboseLevel() > 8)
				merr.write(
					"Failed - End of matching string.\n");
			charsmatched=matchedpos;
			return (matched);
		}
		if (curstate->numnodes == 0)	// No sense to continue
		{
			if (VerboseLevel() > 8)
				merr.write(
					"Failed - out of states.\n");
			charsmatched=matchedpos;
			return (matched);
		}

		if (VerboseLevel() > 8)
		{
		Buffer	b;

			b="Matching character: ";

			if (nextChar <= ' ' || nextChar > 127)
			{
				b += '#';
				b.append((unsigned long)nextChar);
			}
			else	b += (char)nextChar;
			b += '\n';
			b += '\0';
			merr.write(b);
		}
		++charsmatched;

		if (!casesensitive)
			nextChar=tolower(nextChar);

		nextstate->numnodes=0;

		for (n=0; n<curstate->numnodes; n++)
		{
		RegExpNode *p=curstate->nodes[n];

			if (p->thechar == RESET)
			{
				if ((p->reset[nextChar / 8] &
						(1 << (nextChar % 8))) == 0)
				{
					if (casesensitive)	continue;

				int	uchar=toupper(nextChar);
					if ((p->reset[uchar / 8] &
						(1 << (uchar % 8))) == 0)
							continue;
				}
			}
			else
			{
				if (p->thechar != nextChar)
				{
					if (casesensitive)	continue;
				int	uchar=toupper(nextChar);
					if (p->thechar != uchar)
						continue;
				}
			}

		RegExpNode *q=p->next1;

			if (q && nextstate->nodenums[q->id] != charsmatched)
			{
				nextstate->nodes[nextstate->numnodes++]=q;
				nextstate->nodenums[q->id]=charsmatched;
				if (VerboseLevel() > 8)
				{
				Buffer b;

					b="  Transition to state ";
					b.append((unsigned long)q->id);
					b += '\n';
					b += '\0';
					merr.write(b);
				}
			}

			q=p->next2;

			if (q && nextstate->nodenums[q->id] != charsmatched)
			{
				nextstate->nodes[nextstate->numnodes++]=q;
				nextstate->nodenums[q->id]=charsmatched;
				if (VerboseLevel() > 8)
				{
				Buffer b;

					b="  Transition to state ";
					b.append((unsigned long)q->id);
					b += '\n';
					b += '\0';
					merr.write(b);
				}
			}
		}

	ReEval *swap=curstate; curstate=nextstate; nextstate=swap;
	}
}
