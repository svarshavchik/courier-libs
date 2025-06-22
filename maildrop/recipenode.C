/*
** Copyright 1998 - 2023 Double Precision, Inc.
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

static void parse_backslash(const std::string &, std::string &);

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

void	RecipeNode::Evaluate(Recipe &r, std::string &b)
{
RecipeNode	*c;

	b.clear();
	switch (nodeType)	{
	case statementlist:

		for (c=firstChild; c; c=c->nextSibling)
		{
			b.clear();
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
		std::string	s;

			s=firstChild->str;
			s += "=\"";
			s += b;
			s += "\"";
			r.errmsg(*this, s.c_str());
		}
		SetVar(firstChild->str, b);
		break;
	case regexpr:
		EvaluateRegExp(r, b, nullptr);
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
		std::string ba1, ba2;
		std::string debug;

			firstChild->Evaluate(r, ba1);
			firstChild->nextSibling->Evaluate(r, ba2);

			if (VerboseLevel() > 5)
			{
				debug="Operation on: ";
				debug += ba1;
				debug += " and ";
				debug += ba2;
			}

			maildrop.sigfpe=0;
			a1=atof( ba1.c_str() );
			a2=atof( ba2.c_str() );
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
			add_number(b, a1);
			if (VerboseLevel() > 5)
			{
				debug += ", result is ";
				debug += b;
				r.errmsg(*this, debug.c_str());
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
		std::string ba1, ba2;
		std::string debug;

			firstChild->Evaluate(r, ba1);
			firstChild->nextSibling->Evaluate(r, ba2);

			if (VerboseLevel() > 5)
			{
				debug="Operation on: ";
				debug += ba1;
				debug += " and ";
				debug += ba2;
			}


			int	n=strcmp( ba1.c_str(), ba2.c_str());

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
			add_integer(b, n);
			if (VerboseLevel() > 5)
			{
				debug += ", result is ";
				debug += b;
				r.errmsg(*this, debug.c_str());
			}
			break;
		}
	case logicalnot:
		if (!firstChild)
			throw "Internal error in evaluate unary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - logical not.";
			r.errmsg(*this, debug.c_str());
		}
		{
		int n= !boolean(b);

			b.clear();
			add_integer(b, n );
		}
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: logical not=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		break;
	case bitwisenot:
		if (!firstChild)
			throw "Internal error in evaluate unary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - bitwise not.";
			r.errmsg(*this, debug.c_str());
		}
		maildrop.sigfpe=0;
		{

			long n=atol( b.c_str());

			n= ~n;
			b.clear();
			if (n < 0)
			{
				b += "-";
				n= -n;
			}
			add_integer(b, n );
		}
		if (maildrop.sigfpe)
		{
			r.errmsg(*this, "Numerical exception.\n");
		}
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: bitwise not=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		break;
	case logicalor:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: logical or, left hand side=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		if (!boolean(b))
		{
			if (VerboseLevel() > 5)
			{
			std::string	debug;

				debug="Operation: logical or, evaluating right hand size.";
				r.errmsg(*this, debug.c_str());
			}
			firstChild->nextSibling->Evaluate(r, b);
		}
		if (VerboseLevel() > 5)
		{
		std::string debug;

			debug="Operation: logical or, result=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		break;
	case logicaland:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate binary.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: logical and, left hand side=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		if (boolean(b))
		{
			if (VerboseLevel() > 5)
			{
			std::string	debug;

				debug="Operation: logical and, evaluating right hand size.";
				r.errmsg(*this, debug.c_str());
			}
			firstChild->nextSibling->Evaluate(r, b);
		}
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: logical and, result=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		break;
	case concat:
		{
		std::string	bb;

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
		std::string	debug;

			debug="Operation on: ";
			debug += b;
			debug += " - strlength.";
			r.errmsg(*this, debug.c_str());
		}
		{
		unsigned long	n=b.size();

			b.clear();
			add_integer(b, n);
		}
		if (VerboseLevel() > 5)
		{
		std::string	debug;

			debug="Operation: strlength=";
			debug += b;
			r.errmsg(*this, debug.c_str());
		}
		break;
	case	strsubstr:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in evaluate unary.";
		{
		std::string	bb, cc;

			firstChild->Evaluate(r, bb);
			firstChild->nextSibling->Evaluate(r, cc);

			long n=atol( cc.c_str());

			if (VerboseLevel() > 5)
			{
			std::string	debug;

				debug="Operation on: ";
				debug += bb;
				debug += " - strsubstr ";
				add_integer(debug, n);
				r.errmsg(*this, debug.c_str());
			}

		long l=bb.size();

			if (n < 0 || n > l)	n=l;
			b.append( bb.c_str() + n,
				  bb.c_str() + l);
			if (firstChild->nextSibling->nextSibling)
			{
				firstChild->nextSibling->nextSibling->
							Evaluate(r, cc);
				n=atol( cc.c_str());
				if ((size_t)n < b.size())
					b.resize(n);

				if (VerboseLevel() > 5)
				{
				std::string	debug;

					debug="Operation on: ";
					debug += bb;
					debug += " - strsubstr chop ";
					add_integer(debug, n);
					r.errmsg(*this, debug.c_str());
				}
			}
			if (VerboseLevel() > 5)
			{
			std::string	debug;

				debug="Operation: ";
				debug += " strsubstr=";
				debug += b;
				r.errmsg(*this, debug.c_str());
			}
		}
		break;
	case strregexp:
		EvaluateStrRegExp(r, b, nullptr);
		break;
	case whileloop:
		if (!firstChild || !firstChild->nextSibling)
			throw "Internal error in while loop.";

		for (;;)
		{
			if (VerboseLevel() > 3)
			{
				r.errmsg(*this, "Evaluating WHILE condition.");
			}
			firstChild->Evaluate(r,b);
			if (VerboseLevel() > 3)
			{
			std::string	buf;

				buf="While condition evaluated, result=";
				buf += b;
				r.errmsg(*this, buf.c_str());
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
		std::string	buf;

			buf="Evaluating IF condition.";
			r.errmsg(*this, buf.c_str());
		}
		firstChild->Evaluate(r,b);

		if (VerboseLevel() > 3)
		{
		std::string	buf;

			buf="IF evaluated, result=";
			buf += b;
			r.errmsg(*this, buf.c_str());
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
		if (delivery(b.c_str()) < 0)
			throw "Unable to deliver to mailbox.";
		throw (extract_int(GetVar("EXITCODE"), "0"));
	case delivercc:
		if (!firstChild)
			throw "Internal error in delivery statement.";
		firstChild->Evaluate(r,b);
		if (delivery(b.c_str()) < 0)
			throw "Unable to deliver to mailbox.";
		b = "0";
		break;
	case xfilter:
		if (!firstChild)
			throw "Internal error in xfilter statement.";
		firstChild->Evaluate(r,b);
		if (VerboseLevel() > 0)
			merr << "maildrop: Filtering through xfilter " <<
				b.c_str() << "\n";
		if (filter(b.c_str()) < 0)
			throw "Unable to filter message.";
		b = "0";
		break;
	case system:
		if (!firstChild)
			throw "Internal error in system statement.";
		firstChild->Evaluate(r,b);
		if (VerboseLevel() > 0)
			merr << "maildrop: Executing system command " <<
				b.c_str() << "\n";
		executesystem(b.c_str());
		b = "0";
		break;
	case exception:
		if (!firstChild)
			throw "Internal error in delivery statement.";
		try
		{
			if (VerboseLevel() > 3)
			{
			std::string	buf;

				buf="Trapping exceptions.";
				r.errmsg(*this, buf.c_str());
			}
			firstChild->Evaluate(r, b);
			b="";
			if (VerboseLevel() > 3)
			{
			std::string	buf;

				buf="Exception trapping removed.";
				r.errmsg(*this, buf.c_str());
			}
		}
		catch (const char *p)
		{
			b=p;
			if (VerboseLevel() > 3)
			{
			std::string	buf;

				buf="Trapped exception.";
				r.errmsg(*this, buf.c_str());
			}
		}
#if NEED_NONCONST_EXCEPTIONS
		catch (char *p)
		{
			if (VerboseLevel() > 3)
			{
			std::string	buf;

				buf="Trapped exceptions.";
				r.errmsg(*this, buf.c_str());
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
			std::string	buf;

				buf="Trapped exception.";
				r.errmsg(*this, buf.c_str());
			}
		}
		break;
	case echo:
		if (!firstChild)
			throw "Internal error in echo statement.";
		firstChild->Evaluate(r, b);
		{
		std::string	s;

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
		std::string	s;

			s="Creating dotlock ";
			s += b;
			r.errmsg(*this, s.c_str());
		}
		{
			DotLock	d;

			{
				block_sigalarm pause;

				d.Lock(b.c_str());
			}
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
		std::string	s;

			s="Creating flock ";
			s += b;
			r.errmsg(*this, s.c_str());
		}
		{
			FileLock	filelock;

			filelock.Lock(b.c_str());
			firstChild->nextSibling->Evaluate(r, b);
		}
		break;
	case logfile:
		if (!firstChild)
			throw "Internal error in logfile statement.";
		firstChild->Evaluate(r, b);
		if (VerboseLevel() > 3)
		{
		std::string	s;

			s="Opening logfile ";
			s += b;
			r.errmsg(*this, s.c_str());
		}
		maildrop.logfile.Close();
		if (maildrop.logfile.Open(b.c_str(),
					  O_CREAT | O_WRONLY | O_APPEND,
			0600) < 0)
			throw "Unable to create log file.";
		break;
	case log:
		if (!firstChild)
			throw "Internal error in logfile statement.";
		firstChild->Evaluate(r, b);
		{
		std::string	s;

			parse_backslash(b, s);
			log_line(s);
		}
		break;
	case importtoken:
		if (!firstChild)
			throw "Internal error in import statement.";
		firstChild->Evaluate(r, b);
		{
		std::string	s;

			if (VerboseLevel() > 3)
			{
				s="import ";
				s += " \"";
				s += b;
				s += "\"";
				r.errmsg(*this, s.c_str());
			}

			s=b;


			const char *name=s.c_str();
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
		std::string	s;

			s="Opening include file ";
			s += b;
			r.errmsg(*this, s.c_str());
		}
		{
		Recipe	r;
		Lexer	in;
		MaildropSaveEM save_embedded_mode;
		static const char embedded_mode_directory[]=ETCDIR "/maildroprcs/";

			if (strncmp( b.c_str(), embedded_mode_directory,
				sizeof(embedded_mode_directory)-1) == 0 &&
				strchr( b.c_str(), '.') == 0)
			{
				maildrop.embedded_mode=0;
				maildrop.reset_vars();
			}
			if (in.Open( b.c_str() ) < 0)
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
		throw extract_int( GetVar("EXITCODE"), "0");
	case foreach:
		if (!firstChild || !firstChild->nextSibling ||
			( firstChild->nodeType != regexpr &&
				firstChild->nodeType != strregexp))
			throw "Internal error in foreach statement.";
		{
			foreach_t foreachbuf;

			if (firstChild->nodeType == regexpr)
				firstChild->EvaluateRegExp(r, b, &foreachbuf);
			else
				firstChild->EvaluateStrRegExp(r,b,&foreachbuf);

			for (auto &matches:foreachbuf)
			{
				std::string varname="MATCH";

				unsigned long n=0;

				for (auto &v:matches)
				{
					SetVar(varname, v);

					varname="MATCH";
					add_integer(varname, ++n);
				}
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
		std::string	expr, file, opts;

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
		std::string	bb;
		const char *p=b.c_str();
		auto	l=b.size();

			while (l)
			{
				--l;
				bb.push_back( tolower( *p++ ));
			}
			b=bb;
		}
		break;
	case to_upper:
		if (!firstChild)
			throw "Internal error in toupper statement.";
		firstChild->Evaluate(r, b);
		{
		std::string	bb;
		const char *p=b.c_str();
		auto	l=b.size();

			while (l)
			{
				--l;
				bb.push_back( toupper( *p++ ));
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
			b.clear();
			b.push_back('1');
		}
		else
		{
			b.clear();
			b.push_back('0');
		}
		break;
#ifdef	DbObj
	case gdbmopen:
		if (!firstChild)
			throw "Internal error in evaluate gdbmopen.";
		firstChild->Evaluate(r, b);
		{
			const char *filename=b.c_str();
			const char *openmode="R";
			std::string	bb;

			if (firstChild->nextSibling)
			{
				firstChild->nextSibling->Evaluate(r, bb);
				openmode=bb.c_str();
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
			b.clear();
			if (n < 0) { b += "-"; n= -n; }
			add_integer(b, n );
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
		std::string	optbuf;

			if (firstChild->nextSibling)
				firstChild->nextSibling->Evaluate(r, optbuf);


		char	*result=r.gdbm_file.Fetch(
				b.c_str(), b.size(), result_size,
				optbuf.c_str());

			b.clear();
			if (result)
			{
				b.append(result, result+result_size);
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
		std::string	key, val;

			firstChild->Evaluate(r, key);
			firstChild->nextSibling->Evaluate(r, val);

			int	n=r.gdbm_file.Store(key.c_str(), key.size(),
						    val.c_str(), val.size(),
						    "R");

			b.clear();
			if (n < 0) { b += "-"; n= -n; }
			add_integer(b, n );
		}
		break;
#else
	case gdbmopen:
	case gdbmclose:
	case gdbmfetch:
	case gdbmstore:
		b.clear();
		b.push_back('0');
		break;
#endif
	case timetoken:
		b.clear();
		{
		time_t	t;

			time(&t);
			add_integer(b, t );
		}
		break;
	case unset:
		if (!firstChild)
			throw "Internal error in unset statement.";
		firstChild->Evaluate(r, b);
		{
		std::string	s;

			if (VerboseLevel() > 3)
			{
				s="unset ";
				s += b;
				r.errmsg(*this, s.c_str());
			}

			UnsetVar(b);
		}
		break;
	}
}

void RecipeNode::EvaluateRegExp(Recipe &r, std::string &b, foreach_t *foreachp)
{
Search	c;
std::string	buf1, buf2;

	ParseRegExp(str, buf1, buf2);

	dollarexpand(r, buf1);
	if (c.find( *maildrop.msgptr, maildrop.msginfo,
		    buf1.c_str(), buf2.c_str(), foreachp))
	{
		c.score=0;
		r.errmsg(*this, "Syntax error in /pattern/.\n");
	}
	else if (VerboseLevel() > 3)
	{
	std::string	buf;

		buf="Search of ";
		buf += buf1.c_str();
		buf += " = ";
		add_number(buf, c.score);
		r.errmsg(*this, buf.c_str());
	}
	add_number(b, c.score);
}

void RecipeNode::EvaluateStrRegExp(Recipe &r, std::string &b, foreach_t *foreachp)
{
	if (!firstChild || !firstChild->nextSibling ||
		firstChild->nextSibling->nodeType != regexpr)
		throw "Internal error in evaluate =~.";

Search	c;
std::string	buf1, buf2;

	firstChild->Evaluate(r,str);
	ParseRegExp( firstChild->nextSibling->str, buf1, buf2);

	dollarexpand(r, buf1);
	if (c.find( str.c_str(), buf1.c_str(), buf2.c_str(), foreachp))
	{
		c.score=0;
		r.errmsg(*this, "Syntax error in /pattern/.\n");
	}
	else if (VerboseLevel() > 3)
	{
	std::string	buf;

		buf="Search of ";
		buf += buf1.c_str();
		buf += " = ";
		add_number(buf, c.score);
		r.errmsg(*this, buf.c_str());
	}
	add_number(b, c.score);
}

// Break down /foo/:bar into foo and bar.

void	RecipeNode::ParseRegExp(const std::string &str, std::string &buf1, std::string &buf2)
{
	buf2=str;

	const	char *p;

	p=buf2.c_str();

	if (*p != '/')	throw "Internal error.";
	++p;
	buf1=p;

	size_t	l=buf1.size(), i;

	buf2.clear();
	p=buf1.c_str();
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
	buf1.resize(i);
}

void	RecipeNode::EvaluateString(Recipe &r, std::string &b)
{
std::string	buf;

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
		if (VerboseLevel() > 0)
			merr << "maildrop: Filtering through `" <<
				buf.c_str() << "`\n";
		try
		{
			int	rc=::xfilter(buf.c_str(), 1);
			size_t	l=0;

			if (rc < 0)
			{
				maildrop.savemsgptr->Init();
				break;
			}
			maildrop.savemsgptr->RewindIgnore();

			// Strip leading/trailing spaces.  Newlines are
			// replaced by spaces.

			while ((rc=maildrop.savemsgptr->sbumpc()) >= 0
				&& isspace(rc))
					;	// Skip leading space
			while (rc >= 0)
			{
				if (rc == '\r' || rc == '\n')	rc=' ';
				b.push_back(rc);
				if (!isspace(rc))	l=b.size();
				rc=maildrop.savemsgptr->sbumpc();
			}
			maildrop.savemsgptr->Init();
			b.resize(l);
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

void	RecipeNode::dollarexpand(Recipe &r, std::string &b)
{
	size_t	i, l;
	const char *p=b.c_str();

	for (i=0, l=b.size(); i<l; )
	{
		if (p[i] == '\\' && i+1 < l)
			++i;
		else
		{
			if (p[i] == '$')
			{
				i=dollarexpand(r, b, i);
				l=b.size();
				p=b.c_str();
				continue;
			}
		}
		++i;
	}
}

void RecipeNode::SpecialEscape(std::string &b)
{
	size_t	i, l;
	const char *p=b.c_str();
	std::string	s;

	for (i=0, l=b.size(); i<l; i++)
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
			s.push_back('\\');
			break;
		}
		s.push_back(p[i]);
	}
	b=s;
}

size_t	RecipeNode::dollarexpand(Recipe &r, std::string &b, size_t index)
{
	size_t	l=b.size();

	if (index+1 >= l)	return (index+1);

	const	char *p=b.c_str();
	std::string	varname;
	size_t	j;

	if (p[index+1] == '{')
	{
		j=index+2;
		for (;;)
		{
			if (j >= l || isspace(p[j]))
			{
				varname="Terminating } is missing.\n";
				r.errmsg(*this, varname.c_str());
				return (index+1);
			}
			if (p[j] == '}')
				break;
			varname.push_back(p[j]);
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
			varname.push_back(p[j]);
		}
	}

	std::string	newstr{p, p+index};

	newstr += GetVar(varname);

	auto	newindex=newstr.size();

	newstr.append(p + j, p + l);
	b=newstr;
	return (newindex);
}

// Is arbitrary string true, or false?
// A false is either an empty string, or a number 0.

int RecipeNode::boolean(const std::string &b)
{
	const char *p=b.c_str();
	auto l=b.size();

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

static void parse_backslash(const std::string &in, std::string &s)
{
	const char *p=in.c_str();
	size_t l=in.size();
	int append_newline=1;

	while (l)
	{
	int	c;

		if (*p != '\\' || l == 1)
		{
			s.push_back(*p);
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
			s.push_back(c);
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
		s.push_back( c );
	}
	if (append_newline)
	{
#if	CRLF_TERM
		s.push_back('\r');
#endif
		s.push_back('\n');
	}
}

void RecipeNode::rfc822getaddr(std::string &buf)
{
	rfc822::tokens tokens{buf, [](size_t){}};
	rfc822::addresses addresses{tokens};

	std::string newbuf;

	for (auto &a:addresses)
	{
		if (!a.address.empty())
		{
			a.address.print(std::back_inserter(newbuf));
			newbuf.push_back('\n');
		}
	}
	buf=newbuf;
}

int RecipeNode::rfc822hasaddr(std::string &buf)
{
	std::string	lower_addr;
	std::string	next_line;
	const char *p;
	size_t	l;
	std::string	header;

	for (p=buf.c_str(), l=buf.size(); l; --l)
		lower_addr.push_back(tolower(*p++));

	if (VerboseLevel() > 5)
		merr << "maildrop: hasaddr('" <<
			lower_addr.c_str() << "')\n";

	maildrop.msgptr->Rewind();
	if (maildrop.msgptr->appendline(next_line))	return (0);
	while ( *(p=next_line.c_str()) != '\n')
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
			next_line.clear();
			if (maildrop.msgptr->appendline(next_line))
				return (0);
			continue;
		}
		header=next_line.c_str()+3+l;
		for (;;)
		{
			next_line.clear();
			if (maildrop.msgptr->appendline(next_line))
				return ( rfc822hasaddr(lower_addr.c_str(), header));
			if (*(p=next_line.c_str()) == '\n')	break;
			if ( !isspace(*p))	break;
			header += " ";
			header += next_line;
		}
		if (rfc822hasaddr(lower_addr.c_str(), header))	return (1);
	}
	return (0);
}

int RecipeNode::rfc822hasaddr(const char *addr, std::string &header)
{

	if (VerboseLevel() > 5)
		merr << "maildrop: hasaddr: rfc822 parsing: "
			<< header.c_str() << "\n";

	rfc822::tokens tokens{header, [](size_t){}};

	rfc822::addresses addresses{tokens};

	std::string s;

	for (auto &a:addresses)
	{
		if (a.address.empty())
			continue;

		s.clear();
		a.address.print(std::back_inserter(s));

		for (auto &c:s)
			c=tolower(c);

		if (s == addr)
			return 1;
	}
	return 0;
}

int RecipeNode::dolookup(std::string &strng, std::string &filename, std::string &opts)
{
static std::string	errbuf;
std::string	real_opts;


	if (strchr (opts.c_str(), 'D'))	real_opts.push_back('D');	// Only allow this opt

Mio	fp;

	if (fp.Open(filename.c_str(), O_RDONLY) < 0)
	{
		errbuf="Unable to open ";
		errbuf += filename.c_str();
		errbuf += ".\n";
		throw errbuf.c_str();
	}

	for (;;)
	{
	int	c;
	const	char *p;

		errbuf.clear();
		while ((c=fp.sbumpc()) >= 0 && c != '\r' && c != '\n')
			errbuf.push_back(c);
		if (c < 0 && errbuf.size() == 0)	break;

		p=errbuf.c_str();
		while (*p && isspace(*p))	p++;
		if (!*p || *p == '#')	continue;

		Search	srch;

		if (srch.find( strng.c_str(), p,
			       real_opts.c_str(), nullptr))
		{
			opts=p;		// Convenient buffer

			errbuf="Bad pattern in ";
			errbuf += filename.c_str();
			errbuf += ": ";
			errbuf += opts;
			errbuf += "\n";
			throw errbuf.c_str();
		}
		if (srch.score)
			return (1);
	}
	return (0);
}
