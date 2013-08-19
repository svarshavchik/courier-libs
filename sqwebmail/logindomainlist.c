#include "config.h"
/*
** Copyright 2003 Double Precision, Inc.  See COPYING for
** distribution information.
**
** Original author: Jesse D. Guardiani, wingnet.net
*/

/*
*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"logindomainlist.h"

/* LINELEN is the maximum length of a line in the file we are reading. */
#define		LINELEN 500
#define		NUM_WC_EN_MODS 2

/* constants */
static const int g_delim=':';
static const int g_wildcard_char='*';

/* array of wildcard enabled modifiers */
static const char g_wc_en_mods[NUM_WC_EN_MODS][2] = { "@", "-" };

/* --------------------------------------------------------------
 * Function : mystrsep()
 * Created  : 03/03/03
 * Author   : JDG
 * Purpose  : Emulate the BSD strsep() function.
 * Notes    : 
 * This function is designed to emulate the BSD strsep function 
 * without introducing any doubts that it is a custom 
 * implementation. It's probably quite a bit slower than the 
 * original BSD strsep(), but it'll do for most purposes. 
 * mystrsep's functionality should be nearly identical to the 
 * BSD strsep, except that it takes an int as argument for the 
 * delimiter.
 * -------------------------------------------------------------- */
static char *mystrsep( char **stringp, int delim )
{
	char *orig_stringp=*stringp;

	/* Make sure we return NULL if we are given NULL */
	if (*stringp == NULL) return orig_stringp;

	/* Locate first occurance of delim in stringp */
	*stringp=strchr(*stringp, delim);
	
	/* If no more delimiters were found, return the last substring */
	if (*stringp == NULL) return orig_stringp;

	/* Set that first occurance to NUL */
	**stringp='\0';
	
	/* move pointer in front of NUL character */
	++(*stringp);
	
	/* Return original value of *stringp */
	return orig_stringp;

}


/* --------------------------------------------------------------
 * Function : ldl_haswildcard()
 * Created  : 04/08/03
 * Author   : JDG
 * Purpose  : Determine if the NUL terminated stringp contains
 *            the wildcard character specified in wildcard.
 * Notes    : Returns 0 if wildcard doesn't exist in stringp.
 *            Returns 1 if stringp does contain wildcard.
 * -------------------------------------------------------------- */
static int ldl_haswildcard( char *stringp, int wildcard)
{
	char *wildcardp;

	/* get pointer to wildcard within stringp */
	wildcardp = strchr(stringp, wildcard);

	if (wildcardp == NULL) return 0;

	return 1;
}


/* --------------------------------------------------------------
 * Function : ldl_getfields()
 * Created  : 03/04/03
 * Modified : 04/07/03 by JDG
 * Author   : JDG
 * Purpose  : Retrieve fields from a logindomainlist statement.
 * Notes    : This function is designed to provide a standard
 *            interface to retrieve fields from a logindomainlist
 *            statement line.
 *
 *            This function should never generate a NULL pointer,
 *            even if a field is empty.
 * -------------------------------------------------------------- */
static void ldl_getfields( char *statementp, char *firstfieldp, char *secondfieldp, char *thirdfieldp )
{
	int  delim=g_delim;
	char *tempfirst;
	char *tempsecond;
	char *tempthird;
	char *bufp=statementp;


	/* get pointers to individual fields from file */
	tempfirst=mystrsep(&bufp, delim);
	tempsecond=mystrsep(&bufp, delim);
	tempthird=mystrsep(&bufp, delim);

	/* fix NULL pointers */
	if (tempfirst == NULL) tempfirst="";
	if (tempsecond == NULL) tempsecond="";
	if (tempthird == NULL) tempthird="";

	/* copy strings */
	strcpy(firstfieldp, tempfirst);
	strcpy(secondfieldp, tempsecond);
	strcpy(thirdfieldp, tempthird);
}


/* --------------------------------------------------------------
 * Function : ldl_invalidstatement()
 * Created  : 03/04/03
 * Modified : 04/07/03
 * Author   : JDG
 * Purpose  : Examine line and determine if it is a valid state-
 *            ment.
 * Notes    : This function can be used to filter out comments,
 *            empty lines, and anything else that shouldn't be in
 *            a valid statement line. Returns 1 (one) if
 *            statementp is a comment or other invalid statement.
 *
 *            Returns zero otherwise.
 * -------------------------------------------------------------- */
static int ldl_invalidstatement( char *statementp )
{
	const int TRUE = 1;
	const int FALSE = 0;
	
	/* comments aren't valid statements */
	if (statementp[0] == '#') return TRUE;

	/* empty lines aren't valid statements */
	if (statementp[0] == ' ' ||
	    statementp[0] == '\t' ||
	    statementp[0] == '\n') return TRUE;

	/* extra long lines aren't valid statements either... */
	if (strlen(statementp) > LINELEN) return TRUE;

	/* might be a valid statement line */
	return FALSE;
}


/* --------------------------------------------------------------
 * Function : extract_wildcardvalue()
 * Created  : 04/07/03
 * Author   : JDG
 * Purpose  : compare wildcardedstringp and realstringp. If the
 *            two match, then replace the contents of realstringp
 *            with the contents of the wildcard character in
 *            wildcardedstringp.
 * Notes    : If the substring beforewildcardp appears at the
 *            absolute beginning of realstringp, then
 *            beforewildcardp is deleted from realstring.
 *            If the substring afterwildcardp appears at the
 *            absolute end of realstringp, then afterwildcardp
 *            is deleted from realstringp.
 *            
 *            The string remaining in realstringp represents the
 *            contents of our wildcard character in
 *            wildcardedstringp.
 *
 *            For example, if:
 *
 *            wildcardedstringp = my*domain.com
 *            realstringp       = myexampledomain.com
 *
 *            Then extract_wildcardvalue() will replace the
 *            contents of realstringp with the following string:
 *
 *            example
 *            
 *            If no wildcard character exists in
 *            wildcardedstringp or if wildcardedstringp and
 *            realstringp don't match, extract_wildcardvalue()
 *            will return zero.
 *            
 *            Otherwise, extract_wildcardvalue() returns non
 *            zero.
 * -------------------------------------------------------------- */
static int extract_wildcardvalue( char *wildcardedstringp, char *realstringp, int wildcard) {
	char wildcardedstring[LINELEN]="";
	char *beforewildcardp;
	char *afterwildcardp;
	char *wildcardedstringpp=NULL;


	/* Continue only if there is actually a wildcard character in wildcardedstring */
	if (ldl_haswildcard(wildcardedstringp, wildcard)) {
		/* Copy argument to buffer so as not to modify the original. */
		strcpy(wildcardedstring, wildcardedstringp);

		/* create a pointer to a pointer of a copy*/	
		wildcardedstringpp = wildcardedstring;
		

		/* tokenize wildcardstring with '\0's */
		beforewildcardp=mystrsep(&wildcardedstringpp, wildcard);
		afterwildcardp=mystrsep(&wildcardedstringpp, wildcard);


		if (beforewildcardp != NULL && strcmp(beforewildcardp, "") != 0)
		{
			/* If beforewildcardp string exists at the absolute
			 * beginning of realstring */
			if (strstr(realstringp, beforewildcardp) == realstringp)
			{
				char *tmpp=realstringp;
				char *p;

				/* move pointer to the end of beforewildcardp in
				 * realstring. */
				tmpp=tmpp + strlen(beforewildcardp);

				/* Now, "delete" beforewildcardp from the very
				 * beginning of realstring. Note that we're not
				 * actually deleting it. We're just copying the
				 * remaining string in realstring from the
				 * location that tmpp points to in realstring.
				 * However, this has the same effect as if we
				 * had somehow "deleted" beforewildcardp from
				 * the beginning of realstring. */
				
				p=realstringp;

				while ((*p++= *tmpp++) != 0)
					;
			}
			else
			{
				/* if beforewildcardp does not exist at the
				 * beginning of realstringp, return non zero. */
				return 0;
			}
		}


		if (afterwildcardp != NULL && strcmp(afterwildcardp, "") != 0)
		{
			/* If afterwildcardp exists at the very end of realstring */
			size_t n=strlen(realstringp);
			size_t o=strlen(afterwildcardp);

			if (n >= o &&
			    strcmp(realstringp+n-o, afterwildcardp) == 0)
			{
				char *tmpp=realstringp;

				/* move temp pointer to the end of the NUL
				 * terminated string in realstringp, then
				 * backspace the length of afterwildcardp
				 * and write a NUL character.
				 *
				 * This effectively "deletes"
				 * afterwildcardp from realstringp as far
				 * as any string manipulation functions
				 * that rely on a terminating NUL character
				 * are concerned. */
				tmpp=tmpp + n - o;
				*tmpp='\0';
			}
			else
			{
				/* if afterwildcardp does not exist at the
				 * end of realstringp, return non zero. */
				return 0;
			}
		}


		/* if we made it here, then we must have
		 * successfully removed everything but
		 * the value of the wildcard in
		 * wildcardedstringp from realstringp.
		 *
		 * Return zero. */
		return 1;
	} else {
		/* no wildcard in wildcardedstringp */
		return 0;
	}
}


/* --------------------------------------------------------------
 * Function : replace_wildcard()
 * Created  : 04/07/03
 * Author   : JDG
 * Purpose  : replace wildcard character in wildcardedstringp
 *            with contents of wildcardvalue.
 * Notes    : return non zero on success. return zero otherwise.
 * -------------------------------------------------------------- */
static int replace_wildcard( char *wildcardedstringp, char *wildcardvaluep, int wildcard) {
	char wildcardedstring[LINELEN]="";
	char *beforewildcardp;
	char *afterwildcardp;
	char *wildcardedstringpp=NULL;

	
	/* Continue only if there is actually a wildcard in wildcardedstringp */
	if (ldl_haswildcard(wildcardedstringp, wildcard)) {
		/* Copy wildcardedstringp so as not to modify the original. */
		strcpy(wildcardedstring, wildcardedstringp);

		/* create a pointer to a pointer of a copy */	
		wildcardedstringpp = wildcardedstring;

		
		/* tokenize first field */
		beforewildcardp=mystrsep(&wildcardedstringpp, wildcard);
		afterwildcardp=mystrsep(&wildcardedstringpp, wildcard);

		/* start with a clean slate */
		strcpy(wildcardedstringp, "");

		if (beforewildcardp != NULL &&
		    strcmp(beforewildcardp, "") != 0)
		{
			/* Place string contents of beforewildcardp in wildcardedstringp */
			strcpy(wildcardedstringp, beforewildcardp);
		}

		/* Add wildcardvaluep string to end of wildcardedstringp */
		strncat(wildcardedstringp, wildcardvaluep,
			LINELEN-1-strlen(wildcardedstringp));

		if (afterwildcardp != NULL &&
		    strcmp(afterwildcardp, "") != 0)
		{
			/* Add afterwildcardp string to end of wildcardedstringp */
			strncat(wildcardedstringp, afterwildcardp,
				LINELEN-1-strlen(wildcardedstringp));
		}

		/* all is well */
		return 1;

	} else {
		/* no wildcard in wildcardedstringp */
		return 0;
	}
}


/* --------------------------------------------------------------
 * Function : get_defaultdomainfields()
 * Created  : 02/25/03
 * Modified : 04/07/03 by JDG
 * Author   : JDG
 * Purpose  : Retrieve default domain from 'LOGINDOMAINLIST' file
 *            using either 'SERVER_ADDR' or 'HTTP_HOST' CGI 
 *            variables.
 * Notes    : 
 *
 * LOGINDOMAINLIST file can have the following format:
 * 
 * DOMAIN1:IP1:MODIFIER
 * DOMAIN2:DOMAIN3:MODIFIER
 * DOMAIN4:DOMAIN5:MODIFIER
 * etc...
 *
 * The first field contains the mail domain, and it is this field
 * that appears in the drop down list if a drop down is specified
 * by the modifier field.
 *
 * The second field can contain either an IP address or a domain
 * name. This field is campared against the contents of the
 * HTTP_HOST and/or SERVER_ADDR CGI environment variables. If a
 * match is found, then the first and third (last) fields are
 * written to domainp and modifyp, respectively.
 *
 * The third field, or modifier, can be an "*", an "@", or an
 * rbitrary group identifier string. See README.logindomainlist
 * in the main distribution directory for more details regarding
 * LOGINDOMAINLIST file syntax.
 * -------------------------------------------------------------- */
static void get_defaultdomainfields( FILE *fp, char *domainp, char *modifyp)
{
	char buf[LINELEN]="";
	char *serveraddr=getenv("SERVER_ADDR");
	char *httphost=getenv("HTTP_HOST");

	if (!serveraddr) serveraddr="";
	if (!httphost) httphost="";

	/* rewind the file pointer */
	rewind(fp);

	/* Read one line at a time from fp */
	while (fgets(buf, sizeof(buf), fp))
	{
		int count = 0;
		char firstfield[LINELEN]="";
		char secondfield[LINELEN]="";
		char thirdfield[LINELEN]="";
		/* get the position of the newline (if it exists) */
		char *p=strchr(buf, '\n');

		/* replace the newline with NUL */
		if (*p) *p='\0';

		/* ignore comments, empty lines, etc... */
		if (ldl_invalidstatement(buf)) continue;


		/* get individual fields from line */
		ldl_getfields(buf, firstfield, secondfield, thirdfield);

		/* process any wildcard enabled modifiers */
		for (count = 0; count < NUM_WC_EN_MODS; count++)
		{
			const char *current_modifier = &g_wc_en_mods[count][0];
		
			/* If this record is using wildcard domain mapping... */
			if (strcmp(thirdfield, current_modifier) == 0)
			{
				int  wildcard = g_wildcard_char;

				/* If either the first or second field contains a wildcard char */
				if (ldl_haswildcard(firstfield, wildcard) ||
				    ldl_haswildcard(secondfield, wildcard))
				{
					char finaldomain[LINELEN] = "";
					char wildcardvalue[LINELEN] = "";
					char tempbuf[LINELEN] = "";

					/* extract the string that the wildcard in
					 * secondfield represents when compared
					 * with currentdomain */

					/* seed wildcardvalue with contents of
					 * httphost. Sorry - no IP wildcarding. */
					strcpy( wildcardvalue, httphost);

					/* if secondfield and wildcardvalue match,
					 * extract_wildcardvalue will "chop off"
					 * the text before and after the wildcard
					 * character in secondfield from
					 * wildcardvalue */
					if (extract_wildcardvalue(secondfield, wildcardvalue, wildcard))
					{
						/* wildcardvalue may now contain the final
						 * domain name to use as default domain if
						 * firstfield does NOT contain a wildcard.
						 *
						 * That is why we save the contents of
						 * wildcardvalue in finaldomain here. */
						strcpy(finaldomain, wildcardvalue);
					}
					else
					{
						/* Make sure this wildcardless record
						 * actually matches httphost before doing
						 * anything else */
						if (strcmp(secondfield, httphost) != 0) continue;

						/* we don't have a wildcard in the second
						 * field, so just do a straight copy */
						strcpy(finaldomain, secondfield);
					}

					
					/* seed tempbuf with contents of firstfield */
					strcpy(tempbuf, firstfield);

					/* Replace wildcard character in tempbuf
					 * with contents of wildcardvalue */
					if (replace_wildcard(tempbuf, wildcardvalue, wildcard))
					{

						/* The above replace_wildcard() must have
						 * been seccessful if we're still here,
						 * so save the contents of tempbuf in
						 * finaldomain. */
						strcpy(finaldomain, tempbuf);
					}
					else
					{
						/* we don't have a wildcard in the first
						 * field, so just do a straight copy */
						strcpy(finaldomain, firstfield);
					}

					/* return default domain */
					strcpy(domainp, finaldomain);
					strcpy(modifyp, thirdfield);
					return;
				}
				else
				{
					/* Fall through to matching against serveraddr
					 * and httphost if no wildcards exist in either
					 * firstfield or secondfield */
				}
			}
		}

			
		/* This is reached if the third field (modifier) is NOT a wildcard */
		/* compare second field against CGI variables */
		if (strcmp(secondfield, serveraddr) == 0 ||
		    strcmp(secondfield, httphost) == 0)
		{
			strcpy(domainp, firstfield);
			strcpy(modifyp, thirdfield);
			return;
		}

	}
}


/* --------------------------------------------------------------
 * Function : ldl_displayhidden()
 * Created  : 04/05/03
 * Author   : JDG
 * Purpose  : display an HTML hidden input field with
 *            value="defaultdomain"
 * Notes    : none
 * -------------------------------------------------------------- */
static void ldl_displayhiddenfield( char *defaultdomainp) {
	
	if (strlen(defaultdomainp) > 0)
	{
		/* This is displayed only if defaultdomain is NOT
		 * empty */
		printf("<input type=\"hidden\" name=\"logindomain\" value=\"%s\" />@%s",
				defaultdomainp, defaultdomainp);
	}
	else
	{
		/* Do nothing. This is here so that nothing will
		 * be displayed if defaultdomain is empty. */
	}
}


/* --------------------------------------------------------------
 * Function : ldl_displaytextfield()
 * Created  : 04/07/03
 * Author   : JDG
 * Purpose  : display an HTML text input field with
 *            value="defaultdomain"
 * Notes    : none
 * -------------------------------------------------------------- */
static void ldl_displaytextfield( char *defaultdomainp) {
	
	if (strlen(defaultdomainp) > 0)
	{
		/* This is displayed only if defaultdomain is NOT
		 * empty */
		printf("@<input type=\"text\" name=\"logindomain\" value=\"%s\" size=\"%d\" />",
				defaultdomainp, (int)strlen(defaultdomainp)+2);
	}
	else
	{
		/* Do nothing. This is here so that nothing will
		 * be displayed if defaultdomain is empty. */
	}
}

/* --------------------------------------------------------------
 * Function : ldl_displaydropdown()
 * Created  : 04/05/03
 * Modified : 04/07/03
 * Author   : JDG
 * Purpose  : Display an HTML drop down menu containing only
 *            records from fp with a third field identical to
 *            defaultgroupp.
 *            Set the record with a first field matching
 *            defaultdomainp as 'selected'.
 * Notes    : none
 * -------------------------------------------------------------- */
static void ldl_displaydropdown( FILE *fp, char *defaultdomainp, char *defaultgroupp) {

	char buf[LINELEN];
	
	/* This is a flag that is toggled once the first match has been
	 * made. */
	int firstmatch=0;


	/* rewind file pointer */
	rewind(fp);


	/* Read one line at a time from fp */
	while (fgets(buf, sizeof(buf), fp))
	{
		char firstfield[LINELEN]="";
		char secondfield[LINELEN]="";
		char thirdfield[LINELEN]="";
		/* get the position of the newline (if it exists) */
		char *p=strchr(buf, '\n');

		/* replace the newline with NUL */
		if (*p) *p='\0';

		/* ignore comments, empty lines, etc... */
		if (ldl_invalidstatement(buf)) continue;

		/* get individual fields from file */
		ldl_getfields(buf, firstfield, secondfield, thirdfield);

		/* only display this option if it's group field (thirdfield)
		 * is identical to defaultgroupp. */
		if (strcmp(thirdfield, defaultgroupp) == 0)
		{
			/* Only display the select tag the first time we
			 * find a match. */
			if (firstmatch == 0)
			{
				printf("@<select name=\"logindomain\"><option value=\"\">&nbsp;</option>\n");
				firstmatch=1;
			}

			/* Do not display options with empty first fields. */
			if (strlen(firstfield) > 0)
			{
				/* If 'defaultdomainp' is identical to firstfield
				 * then set this option as 'selected'. */
				if (strcmp(defaultdomainp, firstfield) == 0)
				{
					printf("<option value=\"%s\" selected=\"selected\">%s</option>\n",
							firstfield, firstfield);
				}
				else
				{
					printf("<option value=\"%s\">%s</option>\n",
							firstfield, firstfield);
				}
			}
		}	
	}

	/* Display a closing select tag only if we displayed
	 * a starting select tag */
	if (firstmatch > 0) printf("</select>");
}


/* --------------------------------------------------------------
 * Function : print_logindomainlist()
 * Created  : 03/04/03
 * Modified : 04/07/03 - JDG
 * Author   : JDG
 * Purpose  : parse fp and print proper output.
 * Notes    : none
 * -------------------------------------------------------------- */
void print_logindomainlist( FILE *fp )
{
	char defaultdomain[LINELEN]="";
	char modifierfield[LINELEN]="";

	/* get default domain field and the corresponding default
	 * group field (if applicable) from fp. */	
	get_defaultdomainfields(fp, defaultdomain, modifierfield);


	/* There are basically two ways to graphically display the
	 * default domain.
	 *
	 * 1.) As a hidden field with descriptive text.
	 * 2.) As a drop down menu with a defaulted option.
	 *
	 * The modifiers '@' and '*' display a hidden field.
	 *
	 * If the modifier field contains anything else, then we
	 * consider it's contents a 'group' identifier and we
	 * display a drop down. */
	if (strcmp(modifierfield, "@") == 0)
	{
		/* ----------------------
		 * DISPLAY HIDDEN FIELD
		 * ---------------------- */
		
		ldl_displayhiddenfield(defaultdomain);
	}
	else if (strcmp(modifierfield, "-") == 0)
	{
		/* ----------------------
		 * DISPLAY TEXT FIELD
		 * ---------------------- */

		ldl_displaytextfield(defaultdomain);
	}
	else
	{
		/* ----------------------
		 * DISPLAY DROP DOWN MENU
		 * ---------------------- */

		ldl_displaydropdown(fp, defaultdomain, modifierfield);
	}
}
