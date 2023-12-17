#include	"unicode_config.h"
#include	"courier-unicode.h"

#include	<string.h>
#include	<stdio.h>
#include	<stdlib.h>

struct i {
	size_t n_start;
	size_t n_size;
	char32_t v;
};

#include "unicode_htmlent.h"

static void testsuite()
{
	size_t j;

	for (j=0; j<sizeof(ii)/sizeof(ii[0]); ++j)
	{
		char buf[60];

		memcpy(buf, n + ii[j].n_start, ii[j].n_size);
		buf[ii[j].n_size]=0;

		if (unicode_html40ent_lookup(buf) != ii[j].v)
		{
			fprintf(stderr, "Did not find %s\n", buf);
			exit(1);
		}

		strcat(buf, "X");

		if (unicode_html40ent_lookup(buf) == ii[j].v)
		{
			fprintf(stderr, "Found %s?\n", buf);
			exit(1);
		}

		buf[strlen(buf)-2]=0;

		if (unicode_html40ent_lookup(buf) == ii[j].v)
		{
			fprintf(stderr, "Found %s?\n", buf);
			exit(1);
		}
	}

	if (unicode_html40ent_lookup("#13") != 13 ||
	    unicode_html40ent_lookup("#x100") != 256)
	{
		fprintf(stderr, "numeric lookup failed\n");
		exit(1);
	}

	if (!unicode_isalpha('A') || !unicode_isupper('A') ||
	    !unicode_islower('a') || !unicode_isdigit('0') ||
	    !unicode_isspace(' ') || !unicode_isblank('\t') ||
	    !unicode_ispunct('['))
	{
		fprintf(stderr, "category lookup failed\n");
		exit(1);
	}
}

void testsuite2()
{
	if (unicode_general_category_lookup(0x0102) !=
	    UNICODE_GENERAL_CATEGORY_Lu ||
	    unicode_general_category_lookup(0xE1000) !=
	    UNICODE_GENERAL_CATEGORY_Cn)
	{
		fprintf(stderr, "general category lookup failed\n");
		exit(1);
	}

	if (unicode_derived_alphabetic_lookup(0x00000040) ||
	    !unicode_derived_alphabetic_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_alphabetic_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_case_ignorable_lookup(0x00000026) ||
	    !unicode_derived_case_ignorable_lookup(0x00000027))
	{
		fprintf(stderr, "unicode_derived_case_ignorable_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_cased_lookup(0x00000040) ||
	    !unicode_derived_cased_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_cased_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_changes_when_casefolded_lookup(0x00000040) ||
	    !unicode_derived_changes_when_casefolded_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_changes_when_casefolded_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_changes_when_casemapped_lookup(0x00000040) ||
	    !unicode_derived_changes_when_casemapped_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_changes_when_casemapped_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_changes_when_lowercased_lookup(0x00000040) ||
	    !unicode_derived_changes_when_lowercased_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_changes_when_lowercased_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_changes_when_titlecased_lookup(0x00000060) ||
	    !unicode_derived_changes_when_titlecased_lookup(0x00000061))
	{
		fprintf(stderr, "unicode_derived_changes_when_titlecased_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_changes_when_uppercased_lookup(0x00000060) ||
	    !unicode_derived_changes_when_uppercased_lookup(0x00000061))
	{
		fprintf(stderr, "unicode_derived_changes_when_uppercased_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_default_ignorable_code_point_lookup(0x000000ac) ||
	    !unicode_derived_default_ignorable_code_point_lookup(0x000000ad))
	{
		fprintf(stderr, "unicode_derived_default_ignorable_code_point_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_grapheme_base_lookup(0x0000001f) ||
	    !unicode_derived_grapheme_base_lookup(0x00000020))
	{
		fprintf(stderr, "unicode_derived_grapheme_base_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_grapheme_extend_lookup(0x000002ff) ||
	    !unicode_derived_grapheme_extend_lookup(0x00000300))
	{
		fprintf(stderr, "unicode_derived_grapheme_extend_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_grapheme_link_lookup(0x0000094c) ||
	    !unicode_derived_grapheme_link_lookup(0x0000094d))
	{
		fprintf(stderr, "unicode_derived_grapheme_link_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_id_continue_lookup(0x0000002f) ||
	    !unicode_derived_id_continue_lookup(0x00000030))
	{
		fprintf(stderr, "unicode_derived_id_continue_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_id_start_lookup(0x00000040) ||
	    !unicode_derived_id_start_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_id_start_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_lowercase_lookup(0x00000060) ||
	    !unicode_derived_lowercase_lookup(0x00000061))
	{
		fprintf(stderr, "unicode_derived_lowercase_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_math_lookup(0x0000002a) ||
	    !unicode_derived_math_lookup(0x0000002b))
	{
		fprintf(stderr, "unicode_derived_math_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_uppercase_lookup(0x00000040) ||
	    !unicode_derived_uppercase_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_uppercase_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_xid_continue_lookup(0x0000002f) ||
	    !unicode_derived_xid_continue_lookup(0x00000030))
	{
		fprintf(stderr, "unicode_derived_xid_continue_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_xid_start_lookup(0x00000040) ||
	    !unicode_derived_xid_start_lookup(0x00000041))
	{
		fprintf(stderr, "unicode_derived_xid_start_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_incb_lookup(0x00000914)
	    != UNICODE_DERIVED_INCB_NONE)
	{
		fprintf(stderr, "unicode_derived_incb_lookup failed.\n");
		exit(1);
	}

	if (unicode_derived_incb_lookup(0x00000915)
	    != UNICODE_DERIVED_INCB_CONSONANT)
	{
		fprintf(stderr, "unicode_derived_incb_lookup failed.\n");
		exit(1);
	}
	if (unicode_derived_incb_lookup(0x00000300)
	    != UNICODE_DERIVED_INCB_EXTEND)
	{
		fprintf(stderr, "unicode_derived_incb_lookup failed.\n");
		exit(1);
	}
	if (unicode_derived_incb_lookup(0x0000094d)
	    != UNICODE_DERIVED_INCB_LINKER)
	{
		fprintf(stderr, "unicode_derived_incb_lookup failed.\n");
		exit(1);
	}
}

int main(int argc, char **argv)
{
	testsuite();
	testsuite2();
	return 0;
}
