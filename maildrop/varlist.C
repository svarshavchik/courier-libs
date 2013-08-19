#include "config.h"
#include	"varlist.h"
#include	"buffer.h"
#include	"funcs.h"
#include	"maildrop.h"

#include	<string.h>


class Variable {
public:

	Variable *next;
	Buffer	name, value;
	} ;

static Variable *varlist[101];

void UnsetVar(const Buffer &var)
{
int varlen=var.Length();
unsigned n=0;
int i;
const char *p=var;
	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;

	if (var.Length() == 7 &&
		strncmp( (const char *)var, "VERBOSE", 7) == 0)
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

void SetVar(const Buffer &var, const Buffer &value)
{
int varlen=var.Length();
unsigned n=0;
int i;
const char *p=var;

	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;

	if (var.Length() == 7 &&
		strncmp( (const char *)var, "VERBOSE", 7) == 0)
	{
		maildrop.verbose_level= value.Int("0");
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

static Buffer zero;

const Buffer *GetVar(const Buffer &var)
{
int varlen=var.Length(),i;
unsigned n=0;
const char *p=var;

	for (i=varlen; i; --i)
		n = (n << 1) ^ (unsigned char)*p++;
	n %= sizeof(varlist)/sizeof(varlist[0]);

Variable *v;

	for (v=varlist[n]; v; v=v->next)
		if ( v->name == var)	return ( &v->value );
	return (&zero);
}

const char *GetVarStr(const Buffer &var)
{
static Buffer tempbuf;

	tempbuf= *GetVar(var);
	tempbuf += '\0';
	return (tempbuf);
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
			l += v->name.Length() + v->value.Length()+2;
			++n;
		}

	if ((envp=new char *[n+1]) == NULL)	outofmem();
	if (l && (envdatap=new char[l]) == NULL)	outofmem();

	for (i=n=0; i<sizeof(varlist)/sizeof(varlist[0]); i++)
		for (v=varlist[i]; v; v=v->next)
		{
			envp[n]=envdatap;
			memcpy(envdatap, (const char *)v->name,
							v->name.Length());
			envdatap += v->name.Length();
			*envdatap++ = '=';
			memcpy(envdatap, (const char *)v->value,
							v->value.Length());
			envdatap += v->value.Length();
			*envdatap++ = 0;
			n++;
		}
	envp[n]=0;
	return (envp);
}
