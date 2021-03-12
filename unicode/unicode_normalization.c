/*
** Copyright 2021 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include	"normalization_defs.h"
#include	"normalization.h"

/*
** Lookup NFC quick check.
*/
static int unicode_nfc_qc(char32_t ch)
{
	return unicode_tab_lookup(ch,
				  nfc_qc_starting_indextab,
				  nfc_qc_starting_pagetab,
				  sizeof(nfc_qc_starting_indextab)/
				  sizeof(nfc_qc_starting_indextab[0]),
				  nfc_qc_rangetab,
				  sizeof(nfc_qc_rangetab)/
				  sizeof(nfc_qc_rangetab[0]),
				  nfc_qc_classtab,
				  UNICODE_NFC_QC_Y);
}

/*
** Lookup nfkc quick check.
*/
static int unicode_nfkc_qc(char32_t ch)
{
	return unicode_tab_lookup(ch,
				  nfkc_qc_starting_indextab,
				  nfkc_qc_starting_pagetab,
				  sizeof(nfkc_qc_starting_indextab)/
				  sizeof(nfkc_qc_starting_indextab[0]),
				  nfkc_qc_rangetab,
				  sizeof(nfkc_qc_rangetab)/
				  sizeof(nfkc_qc_rangetab[0]),
				  nfkc_qc_classtab,
				  UNICODE_NFKC_QC_Y);
}

/*
** Lookup a character's canonical combining class.
*/

uint8_t unicode_ccc(char32_t ch)
{
	return unicode_tab_lookup(ch,
				  ccc_starting_indextab,
				  ccc_starting_pagetab,
				  sizeof(ccc_starting_indextab)/
				  sizeof(ccc_starting_indextab[0]),
				  ccc_rangetab,
				  sizeof(ccc_rangetab)/
				  sizeof(ccc_rangetab[0]),
				  ccc_classtab,
				  0);
}

/*
** Look up a character's decomposition
*/

static const struct decomposition_info *decomp_lookup_search(char32_t ch)
{
	size_t hash;
	size_t i;

	if (ch == 0)
		return NULL;

	hash=(size_t)(ch % (sizeof(decomp_lookup)
			    /sizeof(decomp_lookup[0])));


	for (i=0; i<sizeof(decomp_lookup[hash])/sizeof(decomp_lookup[hash][0]);
	     ++i)
	{
		if (decomp_lookup[hash][i].ch == ch)
			return &decomp_lookup[hash][i];
	}
	return NULL;
}

unicode_canonical_t unicode_canonical(char32_t ch)
{
	const struct decomposition_info *info=decomp_lookup_search(ch);
	unicode_canonical_t ret;

	memset(&ret, 0, sizeof(ret));

	if (info)
	{
		ret.format=info->decomp_type;
		ret.n_canonical_chars=info->decomp_size;
		ret.canonical_chars=decompositions+info->decomp_index;
	}
	return ret;
}

/*
** Scan through a string, finding each character to decompose.
**
** This invokes a provided callback, for each character that's getting
** decomposed.
*/

static void search_for_decompose(unicode_decomposition_t *info,
				 void (*f)(unicode_decomposition_t *,
					   size_t,
					   const struct decomposition_info *,
					   void *),
				 void *arg)
{
	size_t i;

	for (i=0; i<info->string_size; ++i)
	{
		const struct decomposition_info *comp_info;

		/*
		** Skip over characters that have a quick check flag set.
		*/
		if (info->decompose_flags & UNICODE_DECOMPOSE_FLAG_QC)
		{
			if (info->decompose_flags &
			    UNICODE_DECOMPOSE_FLAG_COMPAT
			    ? unicode_nfkc_qc(info->string[i])
			    == UNICODE_NFKC_QC_Y
			    : unicode_nfc_qc(info->string[i])
			    == UNICODE_NFC_QC_Y)
				continue;
		}

		comp_info=decomp_lookup_search(info->string[i]);
		if (comp_info &&
		    /* Should be the case: */
		    comp_info->decomp_size > 0 &&

		    /*
		    ** Pick only canonical decompositions, unless we're
		    ** doing a compatibility decomposition.
		    */
		    (comp_info->decomp_type == UNICODE_CANONICAL_FMT_NONE ||
		     info->decompose_flags &
		     UNICODE_DECOMPOSE_FLAG_COMPAT))
			(*f)(info, i, comp_info, arg);
	}
}

/*
** Metadata we store about the decomposition.
*/

struct decompose_meta {

	/* Number of characters, in total, that we are decomposing. */
	size_t nchars;

	/* And their indices in the original string. */
	size_t *indexes;

	/*
	** For each character how many we're adding, in addition to the
	** character being decomposed. This is 0 for a 1:1 decomposition.
	**/
	size_t *sizes;

	/*
	** And the information about each individual decomposition.
	*/
	const struct decomposition_info **infos;
};

/* Pass 1: count the number of characters to decompose. */

static void decompose_meta_count(unicode_decomposition_t *info,
				 size_t i,
				 const struct decomposition_info *cinfo,
				 void *arg)
{
	struct decompose_meta *ptr=(struct decompose_meta *)arg;

	++ptr->nchars;
}

/* Pass 2: compile a list of characters to decompose. */

static void decompose_meta_save(unicode_decomposition_t *info,
				size_t i,
				const struct decomposition_info *cinfo,
				void *arg)
{
	struct decompose_meta *ptr=(struct decompose_meta *)arg;

	ptr->indexes[ptr->nchars]=i;
	ptr->sizes[ptr->nchars]=cinfo->decomp_size-1;
	ptr->infos[ptr->nchars]=cinfo;

	++ptr->nchars;
}

size_t unicode_decompose_reallocate_size(unicode_decomposition_t *info,
					 const size_t *sizes,
					 size_t n)
{
	size_t i;
	size_t new_size=info->string_size;

	for (i=0; i<n; ++i)
		new_size += sizes[i];

	return new_size;
}

static int unicode_decompose_reallocate(unicode_decomposition_t *info,
					 const size_t *offsets,
					 const size_t *sizes,
					 size_t n)
{
	size_t new_size=unicode_decompose_reallocate_size(info, sizes, n);
	char32_t *str=(char32_t *)realloc(info->string,
					  (new_size+1) * sizeof(char32_t));

	if (str == NULL)
		return -1;

	info->string=str;
	str[new_size]=0;
	/* If the original was null-terminated, this one will be too,
	** we realloc-ed an extra char32_t */
	return 0;
}

void unicode_decomposition_init(unicode_decomposition_t *info,
				char32_t *string,
				size_t string_size,
				void *arg)
{
	memset(info, 0, sizeof(*info));

	info->string=string;
	if (string && string_size == (size_t)-1)
	{
		for (string_size=0; string[string_size]; ++string_size)
			;
	}

	info->reallocate=unicode_decompose_reallocate;
	info->string_size=string_size;
	info->arg=arg;
}

void unicode_decomposition_deinit(unicode_decomposition_t *info)
{
}

int unicode_decompose(unicode_decomposition_t *info)
{
	int replaced;
	int rc=0;

	do
	{
		struct decompose_meta meta;
		char32_t *string_end, *string_put;
		size_t next_insert_pos;
		size_t i;
		size_t old_pos;

		replaced=0;

		/*
		** Make two passes over the string, on the first pass count
		** the decompositions.
		*/

		meta.nchars=0;
		search_for_decompose(info, decompose_meta_count, &meta);

		if (!meta.nchars)
			continue; /* We're done */

		/* We'll want to make another pass */
		replaced=1;

		/*
		** We have two size_t pointers to allocate dynamically,
		** allocate both of them together.
		*/
		meta.indexes=(size_t *)malloc(meta.nchars * 2 * sizeof(size_t));
		if (!meta.indexes)
		{
			rc= -1;
			break;
		}

		meta.infos=(const struct decomposition_info **)
			malloc(sizeof(const struct decomposition_info *)
			       *meta.nchars);

		if (!meta.infos)
		{
			free(meta.indexes);
			rc= -1;
			break;
		}

		/*
		** And here's the second size_t * that we dynamically allocated
		*/
		meta.sizes=meta.indexes+meta.nchars;

		/* And now make a second pass to load the decompositions */
		meta.nchars=0;

		search_for_decompose(info, decompose_meta_save, &meta);

		rc=(*info->reallocate)(info,
				       meta.indexes, meta.sizes,
				       meta.nchars);

		if (rc)
		{
			/* Error, ensure the code below does nothing */
			meta.nchars=0;
			replaced=0;
		}

		/*
		** Insert decompositions. We make a pass from the end of
		** the string to the beginning of it. We already know what
		** all the decompositions are, on this pass.
		**
		** We'll be copying the characters from the existing string
		** from the end of the string to its beginning, starting with
		** the new ending position.
		**
		** Each time we reach a position where the decomposition
		** occurs, we inject it. In this manner we'll be processing
		** each decomposition starting with the last one in the
		** string, going back to the first one.
		**
		** So, the next_insert_pos that we will do is the
		** last one:
		*/

		next_insert_pos=meta.nchars;

		/*
		** We start shifting over from the end of the string up.
		** Here's the current end of the string.
		*/
		string_end=info->string+info->string_size;
		old_pos=info->string_size;

		/*
		** And compute where we're putting all the characters
		** by adding up all the sizes.
		*/

		string_put=string_end;
		for (i=0; i<meta.nchars; ++i)
		{
			/* While we're here we'll adjust string_size too... */
			info->string_size += meta.sizes[i];
			string_put += meta.sizes[i];
		}

		/* We only need to keep going until we finished shifting. */

		while (next_insert_pos)
		{
			const struct decomposition_info *decompose=NULL;

			if (--old_pos == meta.indexes[next_insert_pos-1])
			{
				/*
				** Reach the next (previous, really) insert
				** position. We only adjust the pointer here,
				** we'll be copying the decomposition below.
				** Here we're just quietly skipping ahead by
				** the number of new characters in the
				** decomposition.
				*/
				--next_insert_pos;
				string_put -= meta.sizes[next_insert_pos];
				decompose=meta.infos[next_insert_pos];
			}
			*--string_put=*--string_end;

			if (decompose)
			{
				/* The decomposition goes here */
				memcpy(string_put,
				       decompositions + decompose->decomp_index,
				       decompose->decomp_size
				       * sizeof(char32_t));
			}
		}

		free(meta.indexes);
		free(meta.infos);

		/*
		** Make another pass, perhaps.
		*/
	} while (replaced);

	return rc;
}

/*
** Canonical compositions are all two characters > one character.
*/

static char32_t lookup_composition(char32_t a, char32_t b)
{
	size_t i, j;

	i=((size_t)a * canonical_mult1
	   + (size_t)b * canonical_mult2)
		% (sizeof(canonical_compositions_lookup) /
		   sizeof(canonical_compositions_lookup[0]));

	j= i+1 < (sizeof(canonical_compositions_lookup) /
		  sizeof(canonical_compositions_lookup[0]))
		? canonical_compositions_lookup[i+1]
		: sizeof(canonical_compositions)/
		sizeof(canonical_compositions[0]);
	i=canonical_compositions_lookup[i];

	while (i<j)
	{
		if (canonical_compositions[i][0] == a &&
		    canonical_compositions[i][1] == b)
		{
			return canonical_compositions[i][2];
		}
		++i;
	}
	return 0;
}

/* Temporary linked list, until all compositions get built. */

struct unicode_compose_info_list {
	struct unicode_compose_info_list *next;
	struct unicode_compose_info *info;
};


/*
** Collect consecutive sequence of composable characters. We cache each
** character's composition level.
*/

struct char_and_level {
	char32_t ch;		/* The character */
	size_t index;		/* Its position in the original string */
	uint8_t level;		/* Its combining level */
};

/*
** A growing buffer of consecutive composition characters
*/

struct chars_and_levels {
	struct char_and_level *ptr;
	size_t size;
	size_t reserved;
};

/*
** Initialize this buffer.
*/
static int chars_and_levels_init(struct chars_and_levels *p)
{
	p->ptr=malloc( sizeof(struct char_and_level)*(p->reserved=1) );
	if (!p->ptr)
		return -1;
	p->size=0;

	return 0;
}

/*
** Add char+level to the buffer, growing it if needed.
*/

static int add_char_and_level(struct chars_and_levels *p, char32_t ch,
			      size_t index,
			      uint8_t level)
{
	if (p->reserved <= p->size)
	{
		size_t n=p->reserved * 2;

		struct char_and_level *ptr=
			realloc(p->ptr, sizeof(struct char_and_level)*n);

		if (!ptr)
			return -1;
		p->reserved=n;
		p->ptr=ptr;
	}
	p->ptr[p->size].ch=ch;
	p->ptr[p->size].index=index;
	p->ptr[p->size].level=level;
	++p->size;
	return 0;
}

/*
** Deallocate the buffer.
*/

static void chars_and_levels_deinit(struct chars_and_levels *p)
{
	if (p->ptr)
		free(p->ptr);
}

static int unicode_composition_init2(const char32_t *string,
				     size_t string_size,
				     int flags,
				     struct chars_and_levels *clptr,
				     struct unicode_compose_info_list ***tail_ptr);

int unicode_composition_init(const char32_t *string,
			     size_t string_size,
			     int flags,
			     unicode_composition_t *info)
{
	/*
	** Initialize a singly-linked unicode_compose_info_list_list.
	**
	** Initialize the tail pointer. We'll be adding onto the tail pointer
	** as we find each composition.
	**
	** Initialize the chars_and_levels buffer.
	*/

	struct unicode_compose_info_list *list=NULL;
	struct unicode_compose_info_list **tail=&list;
	struct chars_and_levels cl;
	int c;

	info->n_compositions=0;
	info->compositions=0;

	if (chars_and_levels_init(&cl))
		return -1;

	/*
	** Call unicode_composition_init2 to do all the work.
	**
	** When it returns we can deinit the chars_and_levels buffer.
	**
	** If it fails we can also deinitialize the linked list, and
	** return a NULL pointer.
	*/
	c=unicode_composition_init2(string, string_size, flags,
				    &cl, &tail);
	chars_and_levels_deinit(&cl);

	if (c == 0)
	{
		struct unicode_compose_info_list *ptr;

		info->n_compositions=0;

		for (ptr=list; ptr; ptr=ptr->next)
			++info->n_compositions;

		if ((info->compositions=(struct unicode_compose_info **)
		    malloc(sizeof(struct unicode_composition_info *)
			   * (info->n_compositions+1))) == NULL)
		{
			c= -1;
			info->n_compositions=0;
		}
	}

	if (c == 0)
	{
		struct unicode_compose_info_list *ptr;
		size_t i=0;

		while (list)
		{
			ptr=list->next;
			info->compositions[i++]=list->info;
			free(list);
			list=ptr;
		}
		info->compositions[i]=NULL;
	}

	if (c)
	{
		while (list)
		{
			struct unicode_compose_info_list *next=list->next;

			free(list->info);
			free(list);
			list=next;
		}
	}

	return c;
}

static int compose_chars_and_levels(const char32_t *starterptr,
				    size_t starter_index,
				    int flags,
				    struct chars_and_levels *clptr,
				    struct unicode_compose_info_list
				    **last_compositionptr,
				    struct unicode_compose_info_list ***tail_ptr);

static int create_new_composition(size_t starter_index,
				  size_t n_combining_marks,
				  struct unicode_compose_info_list **ptr);

static int unicode_composition_init2(const char32_t *string,
				     size_t string_size,
				     int flags,
				     struct chars_and_levels *clptr,
				     struct unicode_compose_info_list ***tail_ptr)
{
	size_t i;
	struct unicode_compose_info_list *last_composition=NULL;

	/*
	** Here we consecutively scan the string and look up each character's
	** composition level.
	**
	** Each time we get a composition level of 0 we update starterptr to
	** point to the starter character, and save its index here.
	*/

	const char32_t *starterptr=NULL;
	size_t starter_index=0;
	for (i=0; i<string_size; ++i)
	{
		uint8_t ccc=unicode_ccc(string[i]);
		char32_t new_char;

		if (ccc == 0)
		{
			/*
			** Starter. If there were any preceding composing
			** characters, then compose them.
			*/
			if (compose_chars_and_levels(starterptr,
						     starter_index,
						     flags,
						     clptr,
						     &last_composition,
						     tail_ptr))
				return -1;

			/*
			** It's possible for a starter to combine with its
			** preceding starter.
			*/

			if (starterptr &&
			    /* Did we just compose this starter? */
			    last_composition &&
			    last_composition->info->index == starter_index &&

			    /*
			    ** Did we compose everything, didn't leave
			    ** any combined marks behind?
			    */
			    last_composition->info->n_composition == 1)
			{
				/*
				** So, check if we can combine with that
				** last starter. *starterptr is the
				** original starter, the new one is here.
				*/
				new_char=lookup_composition
					(last_composition->info->composition[0],
					 string[i]);

				if (new_char != 0)
				{
					/*
					** Just update the composed char.
					*/
					last_composition->info->composition[0]=
						new_char;

					/*
					** And incrementing n_composed.
					** This nukes this starter, as if
					** it was a part of the composition!
					*/
					++last_composition->info->n_composed;
					continue;
				}
			}
			else if (starterptr && starter_index+1 == i &&
				/*
				** Ok, the last starter was not composed,
				** and it was the previous character. The
				** comparison against the starter_index is
				** just, really, a sanity check.
				*/
				 (new_char=
				  lookup_composition(*starterptr,
						     string[i])) != 0)
			{
				/*
				** We'll need to manually create a composition
				** from two starters here.
				*/

				struct unicode_compose_info_list *new_composition;

				if (create_new_composition(starter_index,
							   1, &new_composition))
					return -1;

				last_composition=new_composition;
				**tail_ptr=new_composition;
				*tail_ptr= &new_composition->next;

				new_composition->info->n_composed=2;
				new_composition->info->n_composition=1;
				new_composition->info->composition[0]=new_char;
				continue;
			}
			/*
			** And this is a new starter.
			*/
			starterptr=&string[i];
			starter_index=i;
		}
		else
		{
			/* Add composing characters */

			if (add_char_and_level(clptr, string[i], i, ccc))
				return -1;
		}
	}

	/* We could've finish the string with some composition */

	return compose_chars_and_levels(starterptr,
					starter_index,
					flags,
					clptr,
					&last_composition,
					tail_ptr);
}

/*
** sort combining characters by their canonical combining class
*/

static int compare_levels(const void *a, const void *b)
{
	const struct char_and_level *ca=(const struct char_and_level *)a;
	const struct char_and_level *cb=(const struct char_and_level *)b;

	return ca->level < cb->level ? -1 :
		ca->level > cb->level ? 1

		/* Same combining level, compare their indexes */
		: ca->index < cb->index ? -1
		: ca->index > cb->index ? 1
		: 0;
}

static int create_new_composition(size_t starter_index,
				  size_t n_combining_marks,
				  struct unicode_compose_info_list **ptr)
{
	struct unicode_compose_info_list *c=
		(struct unicode_compose_info_list *)
		malloc(sizeof(struct unicode_compose_info_list));

	if (!c)
		return -1;

	c->info=malloc(sizeof(struct unicode_compose_info)+
		       sizeof(char32_t) * n_combining_marks);

	if (!c->info)
	{
		free(c);
		return -1;
	}

	c->info->index=starter_index;
	c->info->composition=(char32_t *)(c->info+1);
	c->next=NULL;

	/* Worst case: nothing is composed */

	*ptr=c;
	return 0;
}

static int compose_chars_and_levels(const char32_t *starterptr,
				    size_t starter_index,
				    int flags,
				    struct chars_and_levels *clptr,
				    struct unicode_compose_info_list
				    **last_compositionptr,
				    struct unicode_compose_info_list ***tail_ptr)
{
	struct unicode_compose_info_list *new_composition;
	char32_t starter=0;
	size_t i;
	int composed;

	if (clptr->size == 0)
		/* Nothing to do, no composable chars since last starter */
		return 0;

	qsort(clptr->ptr, clptr->size, sizeof(struct char_and_level),
	      compare_levels);

	if (create_new_composition(starter_index,
				   clptr->size,
				   &new_composition))
		return -1;

	composed=0;

	if (starterptr)
	{
		starter=*starterptr;

		for (i=0; i<clptr->size; ++i)
		{
			char32_t new_char=lookup_composition(starter,
							     clptr->ptr[i].ch);

			if (new_char)
			{
				starter=new_char;

				/*
				** These must have a non-0 level, so mark
				** these ones by setting their level to 0.
				*/
				clptr->ptr[i].level=0;
				composed=1;

				if (flags & UNICODE_COMPOSE_FLAG_ONESHOT)
					break;
			}
		}
	}

	/*
	** new_composition->composition is the same size as the
	** number of composable characters. If we composed at
	** least once, we know that we can fit the starter character
	** and whatever was not composed into that buffer.
	*/
	if (composed)
	{
		size_t j;

		new_composition->info->n_composed=clptr->size+1;

		new_composition->info->composition[0]=starter;

		i=1;
		if (!(flags & UNICODE_COMPOSE_FLAG_REMOVEUNUSED))
		{
			for (j=0; j<clptr->size; ++j)
			{
				/*
				** The ones that were used in the composition
				** have their level reset to 0.
				*/
				if (clptr->ptr[j].level)
				{
					new_composition->info->composition[i++]=
						clptr->ptr[j].ch;
				}
			}
		}
		new_composition->info->n_composition=i;
	} else if (!starterptr && (flags & UNICODE_COMPOSE_FLAG_REMOVEUNUSED))
	{
		/*
		** Edge case, this flag is set, composition characters at the
		** beginning of the string. We will create an empty
		** new_composition.
		*/

		new_composition->info->n_composed=clptr->size;
		new_composition->info->n_composition=0;
		composed=1;
	}

	if (composed)
	{
		*last_compositionptr=new_composition;
		**tail_ptr=new_composition;
		*tail_ptr=&new_composition->next;
	}
	else
	{
		free(new_composition->info);
		free(new_composition);
		new_composition=NULL;
	}

	clptr->size=0; /* Start a new list of composable characters */
	return 0;
}

void unicode_composition_deinit(unicode_composition_t *info)
{
	size_t i;

	for (i=0; i<info->n_compositions; ++i)
		free(info->compositions[i]);

	if (info->compositions)
		free(info->compositions);
	info->compositions=0;
	info->n_compositions=0;
}

size_t unicode_composition_apply(char32_t *string,
				 size_t string_size,
				 unicode_composition_t *info)
{
	size_t j=0;
	size_t i;
	size_t c_index=0;

	for (i=0; i<string_size; )
	{
		if (c_index < info->n_compositions &&
		    info->compositions[c_index]->index == i)
		{
			size_t k;
			struct unicode_compose_info *compose=
				info->compositions[c_index++];

			for (k=0; k<compose->n_composition; ++k)
				string[j++]=compose->composition[k];
			i += compose->n_composed;
		}
		else
		{
			string[j++]=string[i++];
		}
	}

	if (j < string_size)
		string[j]=0;
	return j;
}

int unicode_compose(char32_t *string,
		    size_t string_size,
		    int flags,
		    size_t *new_size)
{
	unicode_composition_t info;

	if (unicode_composition_init(string, string_size, flags, &info))
		return -1;

	*new_size=unicode_composition_apply(string, string_size, &info);

	unicode_composition_deinit(&info);

	return 0;
}
