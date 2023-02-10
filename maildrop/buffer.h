#ifndef	buffer_h
#define	buffer_h


#include	"config.h"
#include	<string>

void	add_number(std::string &buf, double val);
void	add_integer(std::string &buf, unsigned long n);
inline void	set_integer(std::string &buf, unsigned long n)
{
	std::string b;

	add_integer(b, n);
	buf=b;
}

int	extract_int(const std::string &buf, const char * =0);

#endif
