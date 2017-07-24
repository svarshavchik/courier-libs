#include "config.h"
#include	"token.h"


static const char *names[]={
		"error",
		"eof",
		"\"...\"",
		"'...'",
		"`...`",
		"=",
		"{",
		"}",
		";",
		"/.../",
                "+",
		"-",
		"*",
		"/",
		"<",
		"<=",
		">",
		">=",
		"==",
		"!=",
		"||",
		"&&",
		"|",
		"&",
		"(",
		")",
		"!",
		"~",
		"lt",
		"le",
		"gt",
		"ge",
		"eq",
		"ne",
		"length",
		"substr",
		",",
		"=~",
		"if",
		"else",
		"elsif",
		"while",
		"to",
		"cc",
		"exception",
		"echo",
		"xfilter",
		"system",
		"dotlock",
		"flock",
		"logfile",
		"log",
		"include",
		"exit",
		"foreach",
		"getaddr",
		"lookup",
		"escape",
		"tolower",
		"toupper",
		"hasaddr",
		"gdbmopen",
		"gdbmclose",
		"gdbmfetch",
		"gdbmstore",
		"time",
		"import",
		"unset"
		} ;

static Buffer namebuf;

const char *Token::Name()
{
	if (type == qstring)
	{
		namebuf="string: \"";
		namebuf += buf;
		namebuf += "\"";
		namebuf.push(0);
		return (namebuf);
	}

	if (type == sqstring)
	{
		namebuf="string: '";
		namebuf += buf;
		namebuf += "'";
		namebuf.push(0);
		return (namebuf);
	}

	if (type == btstring)
	{
		namebuf="string: `";
		namebuf += buf;
		namebuf += "`";
		namebuf.push(0);
		return (namebuf);
	}

	if (type == regexpr)
	{
		namebuf="regexp: ";
		namebuf += buf;
		namebuf.push(0);
		return (namebuf);
	}

unsigned	t=(unsigned)type;

	if (t >= sizeof(names)/sizeof(names[0]))	t=0;
	return (names[t]);
}
