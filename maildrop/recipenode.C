/*
** Copyright 1998 - 2009 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include	"recipenode.h"
#include	"recipe.h"
#include	"varlist.h"
#include	"message.h"
#include	"funcs.h"
#include	"mio.h"
#include	"search.h"
#include	"dotlock.h"
#include	"maildrop.h"
#include	"log.h"
#include	"config.h"
#include	"xconfig.h"
#include	"lexer.h"
#include	"filelock.h"
#include	"rfc822.h"
#include	"mytime.h"
#include	<ctype.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sysexits.h>


extern int xfilter(const char *, int);

static void parse_backslash(const Buffer &, Buffer &);

//////////////////////////////////////////////////////////////////////////
//
// Utility class to save and restore the status of the embedded_mode flag.
//
// Embedded mode can be temporarily disabled by including something from
// ETCDIR/maildroprcs.
//

class MaildropSaveEM {
	int save_mode;
public:
	MaildropSaveEM() { save_mode=maildrop.embedded_mode; }
	~MaildropSaveEM() { maildrop.embedded_mode=save_mode; }
} ;

//////////////////////////////////////////////////////////////////////////

RecipeNode::RecipeNode(RecipeNodeType t) : prevNode(0), nextNode(0),
	parentNode(0), prevSibling(0), nextSibling(0),
	firstChild(0), lastChild(0), linenum(0), nodeType(t)
{
}

void	RecipeNode::AppendSibling(RecipeNode *chld)
{
	chld->parentNode=this;
	chld->nextSibling=0;
	if ( (chld->prevSibling=lastChild) != 0)
		lastChild->nextSibling=chld;
	else
		firstChild=chld;
	lastChild=chld;
}

void	RecipeNode::Evaluate(Recipe &r, Buffer &b)
{
RecipeNode	*c;

	b.reset();
	switch (nodeType)	{
	case statementlist:

		for (c=firstChild; c; c=c->nextSibling)
		{
			b.reset();
			c->Evaluate(r, b);
		}
		break;
	case assignment:
		if (!firstChild || !firstChild->nextSibling ||
			firstChild->nodeType != qstring)
				throw "Internal error in assignment.";
		firstChild->nextSibling->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		Buffer	s;

			s=firstChild->str;
			s += "=\"";
			s += b;
			s += "\"";
			s += '\0';
			r.errmsg(*this, s);
		}
		SetVar(firstChild->str, b);
		break;
	case regexpr:
		EvaluateRegExp(r, b, (Buffer *)NULL);
		break;
	case qstring:
	case sqstring:
	case btstring:
		EvaluateString(r, b);
		break;
	case add:
	case subtract:
	case multiply:
	case divide:
	case lessthan:
	case lessthanoreq:
	case greaterthan:
	case greaterthanoreq:
	case equal:
	case notequal:
	case bitwiseor:
	case bitwiseand:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		{
		double a1, a2;
		Buffer ba1, ba2;
		Buffer debug;

			firstChild->Evaluate(r, ba1);
			firstChild->nextSibling->Evaluate(r, ba2);

			if (VerboseLevel() > 5)
			{
				debug="Operation on: ";
				debug += ba1;
				debug += " and ";
				debug += ba2;
			}

			ba1 += '\0';
			ba2 += '\0';
			maildrop.sigfpe=0;
			a1=atof( (const char *)ba1 );
			a2=atof( (const char *)ba2 );
			switch (nodeType)	{
			case add:
				a1 += a2;
				if (VerboseLevel() > 5)
					debug += " - add";
				break;
			case subtract:
				a1 -= a2;
				if (VerboseLevel() > 5)
					debug += " - subtract";
				break;
			case multiply:
				if (VerboseLevel() > 5)
					debug += " - multiply";
				a1 *= a2;
				break;
			case divide:
				if (VerboseLevel() > 5)
					debug += " - divide";
				a1 /= a2;
				break;
			case lessthan:
				a1=a1 < a2;
				if (VerboseLevel() > 5)
					debug += " - less than";
				break;
			case lessthanoreq:
				a1=a1 <= a2;
				if (VerboseLevel() > 5)
					debug += " - less than or equal";
				break;
			case greaterthan:
				a1=a1 > a2;
				if (VerboseLevel() > 5)
					debug += " - greater than";
				break;
			case greaterthanoreq:
				a1=a1 >= a2;
				if (VerboseLevel() > 5)
					debug += " - greater than or equal";
				break;
			case equal:
				a1= a1 == a2;
				if (VerboseLevel() > 5)
					debug += " - equal";
				break;
			case notequal:
				a1= a1 != a2;
				if (VerboseLevel() > 5)
					debug += " - not equal";
				break;
			case bitwiseor:
				a1= (unsigned long)a1 | (unsigned long)a2;
				if (VerboseLevel() > 5)
					debug += " - bitwise or";
				break;
			case bitwiseand:
				a1= (unsigned long)a1 & (unsigned long)a2;
				if (VerboseLevel() > 5)
					debug += " - bitwise and";
				break;
			default:
				break;
			}
			if (maildrop.sigfpe)
			{
				r.errmsg(*this, "Numerical exception.\n");
				a1=0;
			}
			b.append(a1);
			if (VerboseLevel() > 5)
			{
				debug += ", result is ";
				debug += b;
				debug += '\0';
				r.errmsg(*this, debug);
			}
		}
		break;
	case strlessthan:
	case strlessthanoreq:
	case strgreaterthan:
	case strgreaterthanoreq:
	case strequal:
	case strnotequal:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		{
		Buffer ba1, ba2;
		Buffer debug;

			firstChild->Evaluate(r, ba1);
			firstChild->nextSibling->Evaluate(r, ba2);

			if (VerboseLevel() > 5)
			{
				debug="Operation on: ";
				debug += ba1;
				debug += " and ";
				debug += ba2;
			}

			ba1 += '\0';
			ba2 += '\0';

		int	n=strcmp( (const char *)ba1, (const char *)ba2);

			switch (nodeType)	{
			case strlessthan:
				n= n < 0;
				if (VerboseLevel() > 5)
					debug += " - string less than";
				break;
			case strlessthanoreq:
				n= n <= 0;
				if (VerboseLevel() > 5)
					debug += " - string less than or equal";
				break;
			case strgreaterthan:
				n= n > 0;
				if (VerboseLevel() > 5)
					debug += " - string greater than";
				break;
			case strgreaterthanoreq:
				n= n >= 0;
				if (VerboseLevel() > 5)
					debug += " - string grater than or equal";
				break;
			case strequal:
				n= n == 0;
				if (VerboseLevel() > 5)
					debug += " - string equal";
				break;
			case strnotequal:
				n= n != 0;
				if (VerboseLevel() > 5)
					debug += " - string not equal";
				break;
			default:
				break;
			}
			b.append((unsigned long)n);
			if (VerboseLevel() > 5)
			{
				debug += ", result is ";
				debug += b;
				debug += '\0';
				r.errmsg(*this, debug);
			}
			break;
		}
	case logicalnot:
		if (!firstChild)
			throw "Internal error in evaluate unary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - logical not.";
			debug += '\0';
			r.errmsg(*this, debug);
		}
		{
		int n= !boolean(b);

			b.reset();
			b.append( (unsigned long)n );
		}
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: logical not=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		break;
	case bitwisenot:
		if (!firstChild)
			throw "Internal error in evaluate unary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - bitwise not.";
			debug += '\0';
			r.errmsg(*this, debug);
		}
		maildrop.sigfpe=0;
		{
			b += '\0';

		long n=atol( (const char *) b);

			n= ~n;
			b.reset();
			if (n < 0)
			{
				b += '-';
				n= -n;
			}
			b.append( (unsigned long)n );
		}
		if (maildrop.sigfpe)
		{
			r.errmsg(*this, "Numerical exception.\n");
		}
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: bitwise not=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		break;
	case logicalor:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: logical or, left hand side=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		if (!boolean(b))
		{
			if (VerboseLevel() > 5)
			{
			Buffer	debug;

				debug="Operation: logical or, evaluating right hand size.";
				debug += '\0';
				r.errmsg(*this, debug);
			}
			firstChild->nextSibling->Evaluate(r, b);
		}
		if (VerboseLevel() > 5)
		{
		Buffer debug;

			debug="Operation: logical or, result=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		break;
	case logicaland:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: logical and, left hand side=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		if (boolean(b))
		{
			if (VerboseLevel() > 5)
			{
			Buffer	debug;

				debug="Operation: logical and, evaluating right hand size.";
				debug += '\0';
				r.errmsg(*this, debug);
			}
			firstChild->nextSibling->Evaluate(r, b);
		}
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: logical and, result=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		break;
	case concat:
		{
		Buffer	bb;

			for (c=firstChild; c; c=c->nextSibling)
			{
				c->Evaluate(r, bb);
				b += bb;
			}
		}
		break;
	case strlength:
		if (!firstChild)
			throw "Internal error in evaluate unary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - strlength.";
			debug += '\0';
			r.errmsg(*this, debug);
		}
		{
		unsigned long	n=b.Length();

			b.reset();
			b.append(n);
		}
		if (VerboseLevel() > 5)
		{
		Buffer	debug;

			debug="Operation: strlength=";
			debug += b;
			debug += '\0';
			r.errmsg(*this, debug);
		}
		break;
	case	strsubstr:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate unary.";
		{
		Buffer	bb, cc;

			firstChild->Evaluate(r, bb);
			firstChild->nextSibling->Evaluate(r, cc);
			cc += '\0';

		long n=atol( (const char *) cc);

			if (VerboseLevel() > 5)
			{
			Buffer	debug;

				debug="Operation on: ";
				debug += bb;
				debug += " - strsubstr ";
				debug.append( (unsigned long) n);
				debug += '\0';
				r.errmsg(*this, debug);
			}

		long l=bb.Length();

			if (n < 0 || n > l)	n=l;
			b.append( (const char *)bb + n, l-n);
			if (firstChild->nextSibling->nextSibling)
			{
				firstChild->nextSibling->nextSibling->
							Evaluate(r, cc);
				cc += '\0';
				n=atol( (const char *)cc);
				if (n < b.Length())
					b.Length(n);

				if (VerboseLevel() > 5)
				{
				Buffer	debug;

					debug="Operation on: ";
					debug += bb;
					debug += " - strsubstr chop ";
					debug.append( (unsigned long)n);
					debug += '\0';
					r.errmsg(*this, debug);
				}
			}
			if (VerboseLevel() > 5)
			{
			Buffer	debug;

				debug="Operation: ";
				debug += " strsubstr=";
				debug.append(b);
				debug += '\0';
				r.errmsg(*this, debug);
			}
		}
		break;
	case strregexp:
		EvaluateStrRegExp(r, b, (Buffer *)NULL);
		break;
	case whileloop:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in while loop.";

		for (;;)
		{
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Evaluating WHILE condition.";
				r.errmsg(*this, buf);
			}
			firstChild->Evaluate(r,b);
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="While condition evaluated, result=";
				buf += b;
				buf += '\0';
				r.errmsg(*this, buf);
			}
			if (! boolean(b))	break;

			firstChild->nextSibling->Evaluate(r, b);
		}
		break;
	case ifelse:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in while loop.";
		if (VerboseLevel() > 3)
		{
		Buffer	buf;

			buf="Evaluating IF condition.";
			buf += '\0';
			r.errmsg(*this, buf);
		}
		firstChild->Evaluate(r,b);

		if (VerboseLevel() > 3)
		{
		Buffer	buf;

			buf="IF evaluated, result=";
			buf += b;
			buf += '\0';
			r.errmsg(*this, buf);
		}
		if (boolean(b))
			firstChild->nextSibling->Evaluate(r, b);
		else if (firstChild->nextSibling->nextSibling)
			firstChild->nextSibling->nextSibling->Evaluate(r, b);
		break;
	case deliver:
		if (!firstChild)
			throw "Internal error in delivery statement.";
		firstChild->Evaluate(r,b);
		b += '\0';
		if (delivery(b) < 0)
			throw "Unable to deliver to mailbox.";
		b="EXITCODE";
		throw ( GetVar(b)->Int("0") );
	case delivercc:
		if (!firstChild)
			throw "Internal error in delivery statement.";
		firstChild->Evaluate(r,b);
		b += '\0';
		if (delivery(b) < 0)
			throw "Unable to deliver to mailbox.";
		b = "0";
		break;
	case xfilter:
		if (!firstChild)
			throw "Internal error in xfilter statement.";
		firstChild->Evaluate(r,b);
		b += '\0';
		if (VerboseLevel() > 0)
			merr << "maildrop: Filtering through xfilter " <<
				(const char *)b << "\n";
		if (filter(b) < 0)
			throw "Unable to filter message.";
		b = "0";
		break;
	case system:
		if (!firstChild)
			throw "Internal error in system statement.";
		firstChild->Evaluate(r,b);
		b += '\0';
		if (VerboseLevel() > 0)
			merr << "maildrop: Executing system command " <<
				(const char *)b << "\n";
		executesystem(b);
		b = "0";
		break;
	case exception:
		if (!firstChild)
			throw "Internal error in delivery statement.";
		try
		{
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Trapping exceptions.";
				buf += '\0';
				r.errmsg(*this, buf);
			}
			firstChild->Evaluate(r, b);
			b="";
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Exception trapping removed.";
				buf += '\0';
				r.errmsg(*this, buf);
			}
		}
		catch (const char *p)
		{
			b=p;
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Trapped exception.";
				buf += '\0';
				r.errmsg(*this, buf);
			}
		}
#if NEED_NONCONST_EXCEPTIONS
		catch (char *p)
		{
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Trapped exceptions.";
				buf += '\0';
				r.errmsg(*this, buf);
			}
			b=p;
		}
#endif
		catch (int rc)
		{
			if (rc == 0)
				throw rc;
			rc=0;
			b="";
			if (VerboseLevel() > 3)
			{
			Buffer	buf;

				buf="Trapped exception.";
				buf += '\0';
				r.errmsg(*this, buf);
			}
		}
		break;
	case echo:
		if (!firstChild)
			throw "Internal error in echo statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	s;

			parse_backslash(b, s);
			mout << s;
		}
		break;
	case dotlock:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in dotlock statement.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		Buffer	s;

			s="Creating dotlock ";
			s += b;
			s += '\0';
			r.errmsg(*this, s);
		}
		b += '\0';
		{
		DotLock	d;

			d.Lock(b);
			firstChild->nextSibling->Evaluate(r, b);
			d.Unlock();
		}
		break;
	case flock:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in flock statement.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		Buffer	s;

			s="Creating flock ";
			s += b;
			s += '\0';
			r.errmsg(*this, s);
		}
		b += '\0';
		{
		FileLock	filelock;

			filelock.Lock(b);
			firstChild->nextSibling->Evaluate(r, b);
		}
		break;
	case logfile:
		if (!firstChild)
			throw "Internal error in logfile statement.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		Buffer	s;

			s="Opening logfile ";
			s += b;
			s += '\0';
			r.errmsg(*this, s);
		}
		b += '\0';
		maildrop.logfile.Close();
		if (maildrop.logfile.Open(b, O_CREAT | O_WRONLY | O_APPEND,
			0600) < 0)
			throw "Unable to create log file.";
		break;
	case log:
		if (!firstChild)
			throw "Internal error in logfile statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	s;

			parse_backslash(b, s);
			log_line(s);
		}
		break;
	case importtoken:
		if (!firstChild)
			throw "Internal error in import statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	s;

			if (VerboseLevel() > 3)
			{
				s="import ";
				s += " \"";
				s += b;
				s += "\"";
				s += '\0';
				r.errmsg(*this, s);
			}

			s=b;

			s += '\0';

		const char *name=s;
		const char *val=getenv(name);

			if (!val)	val="";
			s=val;

			SetVar(b, s);
		}
		break;
	case include:
		if (!firstChild)
			throw "Internal error in logfile statement.";
		if (maildrop.includelevel >= 20)
		{
			r.errmsg(*this, "Too many included files.");
			throw EX_TEMPFAIL;
		}

		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		Buffer	s;

			s="Opening include file ";
			s += b;
			s += '\0';
			r.errmsg(*this, s);
		}
		b += '\0';
		{
		Recipe	r;
		Lexer	in;
		MaildropSaveEM save_embedded_mode;
		static const char embedded_mode_directory[]=ETCDIR "/maildroprcs/";

			if (strncmp( (const char *)b, embedded_mode_directory,
				sizeof(embedded_mode_directory)-1) == 0 &&
				strchr( (const char *)b, '.') == 0)
			{
				maildrop.embedded_mode=0;
				maildrop.reset_vars();
			}
			if (in.Open( (const char *)b ) < 0)
				throw "Unable to open include file.";
			if (r.ParseRecipe(in) < 0)
				throw EX_TEMPFAIL;

			try
			{
				++maildrop.includelevel;
				r.ExecuteRecipe();
				--maildrop.includelevel;
			}
			catch (...)
			{
				--maildrop.includelevel;
				throw;
			}
		}
		break;
	case exit:
		b="EXITCODE";
		::exit ( GetVar(b)->Int("0") );
	case foreach:
		if (!firstChild || !firstChild->nextSibling ||
			( firstChild->nodeType != regexpr &&
				firstChild->nodeType != strregexp))
			throw "Internal error in foreach statement.";
		{
		Buffer	foreachbuf;
		Buffer	varname;
		Buffer	varvalue;

			if (firstChild->nodeType == regexpr)
				firstChild->EvaluateRegExp(r, b, &foreachbuf);
			else
				firstChild->EvaluateStrRegExp(r,b,&foreachbuf);

		const char *p=foreachbuf;
		int l=foreachbuf.Length();

			while (l)
			{
				varvalue.reset();

			int	i;

				for (i=0; i<l && p[i]; i++)
					;
				varvalue.append(p, i);
				p += i;
				l -= i;
				if (l)	{ p++; l--; }
				varname="MATCH";
				SetVar(varname, varvalue);
				firstChild->nextSibling->Evaluate(r, b);
			}
		}
		break;
	case getaddr:
		if (!firstChild)
			throw "Internal error in getaddr statement.";
		firstChild->Evaluate(r, b);
		rfc822getaddr(b);
		break;
	case escape:
		if (!firstChild)
			throw "Internal error in escape statement.";
		firstChild->Evaluate(r, b);
		SpecialEscape(b);
		break;
	case lookup:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in lookup statement.";
		{
		Buffer	expr, file, opts;

			firstChild->Evaluate(r, expr);
			firstChild->nextSibling->Evaluate(r, file);
			if ( firstChild->nextSibling->nextSibling )
				firstChild->nextSibling->nextSibling
					->Evaluate(r, opts);
			if (dolookup(expr, file, opts))
				b="1";
			else
				b="0";
		}
		break;
	case to_lower:
		if (!firstChild)
			throw "Internal error in tolower statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	bb;
		const char *p=b;
		int	l=b.Length();

			while (l)
			{
				--l;
				bb.push( tolower( *p++ ));
			}
			b=bb;
		}
		break;
	case to_upper:
		if (!firstChild)
			throw "Internal error in toupper statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	bb;
		const char *p=b;
		int	l=b.Length();

			while (l)
			{
				--l;
				bb.push( toupper( *p++ ));
			}
			b=bb;
		}
		break;
	case hasaddr:
		if (!firstChild)
			throw "Internal error in toupper statement.";
		firstChild->Evaluate(r, b);
		if (rfc822hasaddr(b))
		{
			b.reset();
			b.push('1');
		}
		else
		{
			b.reset();
			b.push('0');
		}
		break;
#ifdef	DbObj
	case gdbmopen:
		if (!firstChild)
			throw "Internal error in evaluate gdbmopen.";
		firstChild->Evaluate(r, b);
		b += '\0';
		{
		const char *filename=b;
		const char *openmode="R";
		Buffer	bb;

			if (firstChild->nextSibling)
			{
				firstChild->nextSibling->Evaluate(r, bb);
				bb += '\0';
				openmode=bb;
			}
			if (maildrop.embedded_mode)
				switch ( *openmode )	{
				case '\0':
				case 'r':
				case 'R':
					break;
				default:
					throw "Open gdbm file for write in embedded mode not allowed.";
				}

		int	n=r.gdbm_file.Open(filename, openmode);
			b.reset();
			if (n < 0) { b += '-'; n= -n; }
			b.append( (unsigned long)n );
		}
		break;
	case gdbmclose:
		r.gdbm_file.Close();
		break;
	case gdbmfetch:
		if (!firstChild)
			throw "Internal error in evaluate gdbmfetch.";
		firstChild->Evaluate(r, b);
		{
		size_t	result_size;
		Buffer	optbuf;

			if (firstChild->nextSibling)
				firstChild->nextSibling->Evaluate(r, optbuf);

			optbuf += '\0';

		char	*result=r.gdbm_file.Fetch(
				(const char *)b, b.Length(), result_size,
				(const char *)optbuf);

			b.reset();
			if (result)
			{
				b.append(result, result_size);
				free(result);
			}
			else if (firstChild->nextSibling &&
				firstChild->nextSibling->nextSibling)
				firstChild->nextSibling->nextSibling
							->Evaluate(r, b);
		}
		break;
	case gdbmstore:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate gdbmstore.";
		{
		Buffer	key, val;

			firstChild->Evaluate(r, key);
			firstChild->nextSibling->Evaluate(r, val);

		int	n=r.gdbm_file.Store(key, key.Length(),
						val, val.Length(), "R");

			b.reset();
			if (n < 0) { b += '-'; n= -n; }
			b.append( (unsigned long)n );
		}
		break;
#else
	case gdbmopen:
	case gdbmclose:
	case gdbmfetch:
	case gdbmstore:
		b.reset();
		b.push('0');
		break;
#endif
	case timetoken:
		b.reset();
		{
		time_t	t;

			time(&t);
			b.append( (unsigned long)t );
		}
		break;
	case unset:
		if (!firstChild)
			throw "Internal error in unset statement.";
		firstChild->Evaluate(r, b);
		{
		Buffer	s;

			if (VerboseLevel() > 3)
			{
				s="unset ";
				s += b;
				s += '\0';
				r.errmsg(*this, s);
			}

			UnsetVar(b);
		}
		break;
	}
}

void RecipeNode::EvaluateRegExp(Recipe &r, Buffer &b, Buffer *foreachp)
{
Search	c;
Buffer	buf1, buf2;

	ParseRegExp(str, buf1, buf2);

	dollarexpand(r, buf1);
	buf1 += '\0';
	buf2 += '\0';
	if (c.find( *maildrop.msgptr, maildrop.msginfo, buf1, buf2, foreachp))
	{
		c.score=0;
		r.errmsg(*this, "Syntax error in /pattern/.\n");
	}
	else if (VerboseLevel() > 3)
	{
	Buffer	buf;

		buf="Search of ";
		buf += (const char *)buf1;
		buf += " = ";
		buf.append(c.score);
		buf += '\0';
		r.errmsg(*this, buf);
	}
	b.append(c.score);
}

void RecipeNode::EvaluateStrRegExp(Recipe &r, Buffer &b, Buffer *foreachp)
{
	if (!firstChild || !firstChild->nextSibling ||
		firstChild->nextSibling->nodeType != regexpr)
		throw "Internal error in evaluate =~.";

Search	c;
Buffer	buf1, buf2;

	firstChild->Evaluate(r,str);
	ParseRegExp( firstChild->nextSibling->str, buf1, buf2);

	dollarexpand(r, buf1);
	buf1 += '\0';
	buf2 += '\0';
	str += '\0';
	if (c.find( str, buf1, buf2, foreachp))
	{
		c.score=0;
		r.errmsg(*this, "Syntax error in /pattern/.\n");
	}
	else if (VerboseLevel() > 3)
	{
	Buffer	buf;

		buf="Search of ";
		buf += (const char *)buf1;
		buf += " = ";
		buf.append(c.score);
		buf += '\0';
		r.errmsg(*this, buf);
	}
	b.append(c.score);
}

// Break down /foo/:bar into foo and bar.

void	RecipeNode::ParseRegExp(const Buffer &str, Buffer &buf1, Buffer &buf2)
{
	buf2=str;
	buf2 += '\0';

const	char *p;

	p=buf2;

	if (*p != '/')	throw "Internal error.";
	++p;
	buf1=p;

int	l=buf1.Length(), i;

	buf1 += '\0';
	buf2.reset();
	p=buf1;
	for (i=0; i<l; i++, p++)
	{
		if (*p == '/')
		{
			if (p[1] == ':')
				buf2=p+2;
			break;
		}
		if (*p == '\\' && i+1 < l)
		{
			++p;
			++i;
		}
	}
	buf1.Length(i);
}

void	RecipeNode::EvaluateString(Recipe &r, Buffer &b)
{
Buffer	buf;

	switch (nodeType)	{
	case qstring:
		buf=str;
		dollarexpand(r, buf);
		b += buf;
		break;
	case sqstring:
		b += str;
		break;
	case btstring:
		buf=str;
		buf += '\0';
		if (VerboseLevel() > 0)
			merr << "maildrop: Filtering through `" <<
				(const char *)buf << "`\n";
		try
		{
		int	rc=::xfilter(buf, 1);
		int	l=0;

			if (rc < 0)
			{
				maildrop.savemsgptr->Init();
				break;
			}
			maildrop.savemsgptr->RewindIgnore();

			// Strip leading/trailing spaces.  Newlines are
			// replaced by spaces.

			while ((rc=maildrop.savemsgptr->get_c()) >= 0
				&& isspace(rc))
					;	// Skip leading space
			while (rc >= 0)
			{
				if (rc == '\r' || rc == '\n')	rc=' ';
				b.push(rc);
				if (!isspace(rc))	l=b.Length();
				rc=maildrop.savemsgptr->get_c();
			}
			maildrop.savemsgptr->Init();
			b.Length(l);
		}
		catch (...)
		{
			maildrop.savemsgptr->Init();
			throw;
		}
		break;
	default:
		break;
	}
}

void	RecipeNode::dollarexpand(Recipe &r, Buffer &b)
{
int	i, l;
const char *p=b;

	for (i=0, l=b.Length(); i<l; )
	{
		if (p[i] == '\\' && i+1 < l)
			++i;
		else
		{
			if (p[i] == '$')
			{
				i=dollarexpand(r, b, i);
				l=b.Length();
				p=b;
				continue;
			}
		}
		++i;
	}
}

void RecipeNode::SpecialEscape(Buffer &b)
{
int	i, l;
const char *p=b;
Buffer	s;

	for (i=0, l=b.Length(); i<l; i++)
	{
		switch (p[i])	{
		case '|':
		case '!':
		case '$':
		case '(':
		case ')':
		case '[':
		case ']':
		case '\\':
		case '+':
		case '*':
		case '?':
		case '.':

		case '&':
		case ';':
		case '`':
		case '\'':
		case '-':
		case '~':
		case '<':
		case '>':
		case '^':
		case '{':
		case '}':
		case '"':
			s.push('\\');
			break;
		}
		s.push(p[i]);
	}
	b=s;
}

int	RecipeNode::dollarexpand(Recipe &r, Buffer &b, int index)
{
int	l=b.Length();

	if (index+1 >= l)	return (index+1);

const	char *p=b;
Buffer	varname;
int	j;

	if (p[index+1] == '{')
	{
		j=index+2;
		for (;;)
		{
			if (j >= l || isspace(p[j]))
			{
				varname="Terminating } is missing.\n";
				varname += '\0';
				r.errmsg(*this, varname);
				return (index+1);
			}
			if (p[j] == '}')
				break;
			varname += p[j];
			++j;
		}
		++j;
	}
	else
	{
		if (!isalnum(p[index+1]) && p[index+1] != '_')
			return (index+1);

		for (j=index+1; j<l; j++)
		{
			if (!isalnum(p[j]) && p[j] != '_')	break;
			varname += p[j];
		}
	}

Buffer	newstr;

	newstr.append(p, index);

const Buffer *bb=GetVar(varname);

	if (bb)	newstr += *bb;

int	newindex=newstr.Length();

	newstr.append(p + j, l-j);
	b=newstr;
	return (newindex);
}

// Is arbitrary string true, or false?
// A false is either an empty string, or a number 0.

int RecipeNode::boolean(const Buffer &b)
{
const char *p=b;
int l=b.Length();

	while (l && isspace(*p))	p++, l--;
	if (l == 0)	return (0);

	if (*p != '0' && *p != '.')	return (1);	// Can't be zero.

	if (*p == '0')
	{
		++p;
		--l;
		if (l && *p == '.')	p++,l--;
		else
		{
			while (l && isspace(*p))	p++, l--;
			if (l)	return (1);	// Some text after 0
			return (0);
		}
	}
	++p, --l;
	if (!l)	return (1);	// Period by itself is ok.
	while (l && *p == '0')	p++, l--;	// Zeroes after .
	while (l && isspace(*p))	p++, l--;
	if (l)	return (1);	// Some text after 0
	return (0);
}

static void parse_backslash(const Buffer &in, Buffer &s)
{
const char *p=in;
int l=in.Length();
int append_newline=1;

	while (l)
	{
	int	c;

		if (*p != '\\' || l == 1)
		{
			s.push(*p);
			p++;
			l--;
			continue;
		}
		++p;
		--l;
		if (*p >= '0' && *p <= '7')
		{
			c=0;
			do
			{
				c=c * 8 + (*p++ - '0');
				--l;
			} while (l && *p >= '0' && *p <= '7');
			s.push(c);
			continue;
		}
		c=backslash_char(*p);
		++p;
		--l;
		if (l == 0 && c == 'c')
		{
			append_newline=0;
			break;
		}
		s.push( c );
	}
	if (append_newline)
	{
#if	CRLF_TERM
		s.push('\r');
#endif
		s.push('\n');
	}
}

void RecipeNode::rfc822getaddr(Buffer &buf)
{
	buf += '\0';

	struct	rfc822t	*p=rfc822t_alloc_new(buf, NULL, NULL);

	if (!p)	outofmem();

	struct	rfc822a	*a=rfc822a_alloc(p);

	if (!a) outofmem();

	try
	{
		int n;
		Buffer newbuf;

		for (n=0; n<a->naddrs; n++)
			if (a->addrs[n].tokens)
			{
				char *p=rfc822_display_addr_tobuf(a, n,
								  NULL);

				if (p)
					try {
					        newbuf += p;
						newbuf += "\n";
					} catch (...)
					{
						free(p);
						throw;
					}

				free(p);
			}

		buf=newbuf;
	}
	catch (...)
	{
		rfc822a_free(a);
		rfc822t_free(p);
		throw;
	}
	rfc822a_free(a);
	rfc822t_free(p);
}

int RecipeNode::rfc822hasaddr(Buffer &buf)
{
Buffer	lower_addr;
Buffer	next_line;
const char *p;
int	l;
Buffer	header;

	for (p=buf, l=buf.Length(); l; --l)
		lower_addr.push(tolower(*p++));
	lower_addr += '\0';

	if (VerboseLevel() > 5)
		merr << "maildrop: hasaddr('" <<
			(const char *)lower_addr << "')\n";

	maildrop.msgptr->Rewind();
	if (maildrop.msgptr->appendline(next_line))	return (0);
	while ( *(p=next_line) != '\n')
	{
	int	c;

		for (l=0; l<7; l++)
		{
			c=tolower(p[l]);
			if (c != "resent-"[l])
				break;
		}
		if (l == 7)	p += 7;
		else	l=0;

		c=tolower (*p);

		if (((c == 't' && tolower(p[1]) == 'o')
				||
		    (c == 'c' && tolower(p[1]) == 'c'))
			&& p[2] == ':')
			;
		else
		{
			next_line.reset();
			if (maildrop.msgptr->appendline(next_line))
				return (0);
			continue;
		}
		next_line += '\0';
		header=(const char *)next_line+3+l;
		for (;;)
		{
			next_line.reset();
			if (maildrop.msgptr->appendline(next_line))
				return ( rfc822hasaddr(lower_addr, header));
			if (*(p=next_line) == '\n')	break;
			if ( !isspace(*p))	break;
			header += ' ';
			header += next_line;
		}
		if (rfc822hasaddr(lower_addr, header))	return (1);
	}
	return (0);
}

int RecipeNode::rfc822hasaddr(const char *addr, Buffer &header)
{
	header += '\0';

	if (VerboseLevel() > 5)
		merr << "maildrop: hasaddr: rfc822 parsing: "
			<< (const char *)header << "\n";

	struct	rfc822t	*p=rfc822t_alloc_new(header, NULL, NULL);

	if (!p)	outofmem();

	struct	rfc822a	*a=rfc822a_alloc(p);

	if (!a)	outofmem();

int	i;
Buffer	rfc822buf;
int	found=0;

	for (i=0; i<a->naddrs; i++)
	{
		char *p=rfc822_getaddr(a, i);

		if (!p)
			continue;

		char *q;

		for (q=p; *q; q++)
			*q=tolower(*q);

		rfc822buf=p;
		free(p);
		rfc822buf.push('\0');
		if (VerboseLevel() > 5)
			merr << "maildrop: hasaddr: rfc822 parsed: "
				<< (const char *)rfc822buf << "\n";
		if (strcmp(addr, rfc822buf) == 0)
		{
			found=1;
			break;
		}
	}
	rfc822a_free(a);
	rfc822t_free(p);
	return (found);
}

int RecipeNode::dolookup(Buffer &strng, Buffer &filename, Buffer &opts)
{
static Buffer	errbuf;
Buffer	real_opts;

	filename += '\0';

	strng += '\0';
	opts += '\0';
	if (strchr (opts, 'D'))	real_opts.push('D');	// Only allow this opt
	real_opts += '\0';

Mio	fp;

	if (fp.Open((const char *)filename, O_RDONLY) < 0)
	{
		errbuf="Unable to open ";
		errbuf += (const char *)filename;
		errbuf += ".\n";
		errbuf += '\0';
		throw (const char *)errbuf;
	}

	for (;;)
	{
	int	c;
	const	char *p;

		errbuf.reset();
		while ((c=fp.get()) >= 0 && c != '\r' && c != '\n')
			errbuf.push(c);
		if (c < 0 && errbuf.Length() == 0)	break;
		errbuf.push(0);

		p=errbuf;
		while (*p && isspace(*p))	p++;
		if (!*p || *p == '#')	continue;

	Search	srch;

		if (srch.find( strng, p, real_opts, (Buffer *)NULL))
		{
			opts=p;		// Convenient buffer

			errbuf="Bad pattern in ";
			errbuf += (const char *)filename;
			errbuf += ": ";
			errbuf += opts;
			errbuf += '\n';
			errbuf += '\0';
			throw (const char *)errbuf;
		}
		if (srch.score)
			return (1);
	}
	return (0);
}
