#include "config.h"
#include	"reeval.h"
#include	"funcs.h"


void	ReEval::init(unsigned maxsize)
{
	if (maxsize > arysize)
	{
	RegExpNode **newnodes=new RegExpNode *[maxsize];

		if (!newnodes)	outofmem();
		if (nodes)	delete[] nodes;
		nodes=newnodes;

	unsigned *newnodenums=new unsigned[maxsize];

		if (!newnodenums)	outofmem();
		if (nodenums)	delete[] nodenums;
		nodenums=newnodenums;
	}

unsigned i;

	for (i=0; i<maxsize; i++)
		nodenums[i]= (unsigned)-1;
}
