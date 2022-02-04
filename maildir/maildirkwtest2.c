#define MAILDIRKW_MOCKTIME()				\
	do {						\
		const char *p=getenv("MOCKTIME");	\
		if (p)					\
			t=atoi(p);			\
	} while (0)

#define MAILDIRKW_MOCKTIME2()					\
	do {							\
		const char *eee=getenv("MOCKTIME");			\
									\
		if (eee && strcmp(eee, "1000") == 0 &&			\
		    strcmp(p, "004") == 0)				\
		{							\
			printf("Faked stale filename\n");		\
			fflush(stdout);					\
			p=NULL;						\
									\
		}							\
	} while(0);							\
									\
	i=NULL;								\
		if (p)

#include "maildirkeywords2.c"
