#define MAILDIRKW_MOCKTIME()				\
	do {						\
		const char *p=getenv("MOCKTIME");	\
		if (p)					\
			t=atoi(p);			\
	} while (0)

#define MAILDIRKW_MOCKTIME2()						\
	do {								\
		static bool first=true;					\
		const char *eee=getenv("MOCKTIME");			\
									\
		if (eee && strcmp(eee, "1000") == 0 &&			\
		    status.second.found_newest &&			\
		    status.first == "004" && first)			\
		{							\
			printf("Faked stale filename\n");		\
			fflush(stdout);					\
			opened=false;					\
			first=false;					\
		}							\
	} while(0);							\

#include "maildirkeywords.C"
