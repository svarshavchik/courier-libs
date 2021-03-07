/*
** Copyright 2011 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"

#define BLOCK_SIZE	256

uint8_t unicode_tab_lookup(char32_t ch,
			   const size_t *unicode_starting_indextab,
			   const char32_t *unicode_starting_pagetab,
			   size_t unicode_tab_sizeof,
			   const uint8_t (*unicode_rangetab)[2],
			   size_t unicode_rangetab_sizeof,
			   const uint8_t *unicode_classtab,
			   uint8_t uclass)
{
	size_t cl=ch / BLOCK_SIZE;

	size_t b=0;
	size_t e=unicode_tab_sizeof;

	while (b < e)
	{
		size_t n=b + (e-b)/2;

		if (cl < unicode_starting_indextab[n])
		{
			e=n;
			continue;
		}
		else if (cl > unicode_starting_indextab[n])
		{
			b=n+1;
			continue;
		}

		const size_t start_pos=unicode_starting_pagetab[n];
		const uint8_t (*p)[2]=unicode_rangetab + start_pos;
		b=0;
		e=(n+1 >= unicode_tab_sizeof
		   ? unicode_rangetab_sizeof
		   : unicode_starting_pagetab[n+1]) - start_pos;
		uint8_t chmodcl= ch & (BLOCK_SIZE-1);

		while (b < e)
		{
			size_t n=b + (e-b)/2;

			if (chmodcl >= p[n][0])
			{
				if (chmodcl <= p[n][1])
				{
					uclass=unicode_classtab ?
						unicode_classtab[start_pos+n]
						: 1;
					break;
				}
				b=n+1;
			}
			else
			{
				e=n;
			}
		}
		break;
	}

	return uclass;
}

uint32_t unicode_tab32_lookup(char32_t ch,
			      const size_t *unicode_starting_indextab,
			      const char32_t *unicode_starting_pagetab,
			      size_t unicode_tab_sizeof,
			      const uint8_t (*unicode_rangetab)[2],
			      size_t unicode_rangetab_sizeof,
			      const uint32_t *unicode_classtab,
			      uint32_t uclass)
{
	size_t cl=ch / BLOCK_SIZE;

	size_t b=0;
	size_t e=unicode_tab_sizeof;

	while (b < e)
	{
		size_t n=b + (e-b)/2;

		if (cl < unicode_starting_indextab[n])
		{
			e=n;
			continue;
		}
		else if (cl > unicode_starting_indextab[n])
		{
			b=n+1;
			continue;
		}

		const size_t start_pos=unicode_starting_pagetab[n];
		const uint8_t (*p)[2]=unicode_rangetab + start_pos;
		b=0;
		e=(n+1 >= unicode_tab_sizeof
		   ? unicode_rangetab_sizeof
		   : unicode_starting_pagetab[n+1]) - start_pos;
		uint8_t chmodcl= ch & (BLOCK_SIZE-1);

		while (b < e)
		{
			size_t n=b + (e-b)/2;

			if (chmodcl >= p[n][0])
			{
				if (chmodcl <= p[n][1])
				{
					uclass=unicode_classtab ?
						unicode_classtab[start_pos+n]
						: 1;
					break;
				}
				b=n+1;
			}
			else
			{
				e=n;
			}
		}
		break;
	}

	return uclass;
}
