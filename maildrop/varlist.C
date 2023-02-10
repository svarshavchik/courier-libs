#include "config.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"funcs.h"
#include	"maildrop.h"

#include	<string.h>


class Variable {
public:

	Variable *next;
	std::string	name, value;
	} ;

static Variable *varlist[101];

void UnsetVar(const std::string &var)
{
	auto varlen=var.size();
	unsigned n=0;
	size_t i;
	const char *p=var.c_str();

	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;

	if (var.size() == 7 &&
		strncmp( var.c_str(), "VERBOSE", 7) == 0)
	{
		maildrop.verbose_level=0;
	}

	n %= sizeof(varlist)/sizeof(varlist[0]);

Variable **v;

	for (v= &varlist[n]; *v; v= &(*v)->next)
		if ( (*v)->name == var )
		{
		Variable *vv= (*v);

			(*v)= vv->next;
			delete vv;
			break;
		}
	return;
}

void SetVar(const std::string &var, const std::string &value)
{
	auto varlen=var.size();
	size_t n=0;
	size_t i;
	const char *p=var.c_str();

	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;

	if (var.size() == 7 &&
		strncmp( var.c_str(), "VERBOSE", 7) == 0)
	{
		maildrop.verbose_level= extract_int(value, "0");
		if (maildrop.isdelivery)	maildrop.verbose_level=0;
	}

	n %= sizeof(varlist)/sizeof(varlist[0]);

Variable *v;

	for (v=varlist[n]; v; v=v->next)
		if ( v->name == var )
		{
			v->value=value;
			return;
		}

	v=new Variable;
	if (!v)	outofmem();
	v->name=var;
	v->value=value;
	v->next=varlist[n];
	varlist[n]=v;
}

static std::string zero;

const std::string *GetVar(const std::string &var)
{
	auto varlen=var.size();
	size_t i, n=0;
	const char *p=var.c_str();

	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;
	n %= sizeof(varlist)/sizeof(varlist[0]);

Variable *v;

	for (v=varlist[n]; v; v=v->next)
		if ( v->name == var)	return ( &v->value );
	return (&zero);
}

const char *GetVarStr(const std::string &var)
{
static std::string tempbuf;

	tempbuf= *GetVar(var);
	return (tempbuf.c_str());
}

// Create environment for a child process.

char	**ExportEnv()
{
unsigned	i,n,l;
Variable	*v;
char	**envp;
char	*envdatap=0;

	for (i=n=l=0; i<sizeof(varlist)/sizeof(varlist[0]); i++)
		for (v=varlist[i]; v; v=v->next)
		{
			l += v->name.size() + v->value.size()+2;
			++n;
		}

	if ((envp=new char *[n+1]) == NULL)	outofmem();
	if (l && (envdatap=new char[l]) == NULL)	outofmem();

	for (i=n=0; i<sizeof(varlist)/sizeof(varlist[0]); i++)
		for (v=varlist[i]; v; v=v->next)
		{
			envp[n]=envdatap;
			memcpy(envdatap, v->name.c_str(),
							v->name.size());
			envdatap += v->name.size();
			*envdatap++ = '=';
			memcpy(envdatap, v->value.c_str(),
							v->value.size());
			envdatap += v->value.size();
			*envdatap++ = 0;
			n++;
		}
	envp[n]=0;
	return (envp);
}
