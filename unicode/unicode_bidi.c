/*
** Copyright 2020 Double Precision, Inc.
** See COPYING for distribution information.
**
*/

#include	"unicode_config.h"
#include	"courier-unicode.h"
#include	<unistd.h>
#include	<stdint.h>
#include	<string.h>
#include	<stdlib.h>

static const char32_t bidi_brackets[][2]={
#include "bidi_brackets.h"
};

static const unicode_bidi_bracket_type_t bidi_brackets_v[]={
#include "bidi_brackets_v.h"
};

static const char32_t bidi_mirroring[][2]={
#include "bidi_mirroring.h"
};

static int bidi_lookup(const char32_t (*p)[2], size_t e, char32_t c,
		       size_t *ret)
{
	size_t b=0;

	while (b < e)
	{
		size_t n=b + (e-b)/2;

		if (c > p[n][0])
		{
			b=n+1;
		}
		else if (c < p[n][0])
		{
			e=n;
		}
		else
		{
			*ret=n;
			return 1;
		}
	}

	return 0;
}

char32_t unicode_bidi_mirror(char32_t c)
{
	size_t ret;

	if (bidi_lookup(bidi_mirroring,
			sizeof(bidi_mirroring)/sizeof(bidi_mirroring[0]),
			c,
			&ret))
	{
		return bidi_mirroring[ret][1];
	}

	return c;
}

char32_t unicode_bidi_bracket_type(char32_t c,
				   unicode_bidi_bracket_type_t *type_ret)
{
	size_t ret;

	if (bidi_lookup(bidi_brackets,
			sizeof(bidi_brackets)/sizeof(bidi_brackets[0]),
			c,
			&ret))
	{
		if (type_ret)
			*type_ret=bidi_brackets_v[ret];

		return bidi_brackets[ret][1];
	}

	if (type_ret)
		*type_ret=UNICODE_BIDI_n;
	return c;
}

#include "bidi_class.h"

#ifdef BIDI_DEBUG
enum_bidi_type_t fudge_unicode_bidi(size_t);
const char *bidi_classname(enum_bidi_type_t);
#endif

#define max_depth 125

typedef enum {
	      do_neutral,
	      do_right_to_left,
	      do_left_to_right
} directional_override_status_t;

#define is_isolate_initiator(c)				\
	((c) == UNICODE_BIDI_TYPE_LRI ||		\
	 (c) == UNICODE_BIDI_TYPE_RLI ||		\
	 (c) == UNICODE_BIDI_TYPE_FSI)

#define is_embedding_initiator(c)			\
	((c) == UNICODE_BIDI_TYPE_LRE ||		\
	 (c) == UNICODE_BIDI_TYPE_RLE ||		\
	 (c) == UNICODE_BIDI_TYPE_LRO ||		\
	 (c) == UNICODE_BIDI_TYPE_RLO)

#define is_explicit_indicator_except_b(c)	\
	( is_isolate_initiator(c) ||		\
	  is_embedding_initiator(c) ||		\
	  (c) == UNICODE_BIDI_TYPE_BN ||        \
	  (c) == UNICODE_BIDI_TYPE_PDF ||       \
	  (c) == UNICODE_BIDI_TYPE_PDI)

#define is_explicit_indicator(c)               \
	( is_explicit_indicator_except_b(c) || \
	  (c) == UNICODE_BIDI_TYPE_B)

/* BD13 implementation */

/* A level run, specified as indexes */

struct level_run {
	size_t start;
	size_t end; /* one past */
};

/* A growing list of level runs */

struct level_runs {
	struct level_run *runs; /* All level runs in the sequence */
	size_t n_level_runs;          /* How many of them */
	size_t cap_level_runs;        /* Capacity of the level runs */
};

static void level_runs_init(struct level_runs *p)
{
	p->runs=0;
	p->n_level_runs=0;
	p->cap_level_runs=0;
}

static void level_runs_deinit(struct level_runs *p)
{
	if (p->runs)
		free(p->runs);
}

static struct level_run *level_runs_add(struct level_runs *p)
{
	if (p->n_level_runs == p->cap_level_runs)
	{
		p->cap_level_runs *= 2;

		if (p->cap_level_runs == 0)
			p->cap_level_runs=1;

		p->runs=(struct level_run *)
			(p->runs ?
			 realloc(p->runs,
				 sizeof(struct level_run) *
				 p->cap_level_runs)
			 :malloc(sizeof(struct level_run) *
				 p->cap_level_runs));
		if (!p->runs)
			abort();
	}

	return p->runs + (p->n_level_runs++);
}

/* An isolating run sequence */

struct isolating_run_sequence_s {
	struct isolating_run_sequence_s *prev, *next; /* Linked list */

	struct level_runs runs;
	unicode_bidi_level_t embedding_level; /* This seq's embedding level */
	enum_bidi_type_t sos, eos;
};

/* An iterator for an isolating run sequence */

typedef struct {

	/* My sequence */
	struct isolating_run_sequence_s *seq;

	/* Which level run I'm on */

	size_t level_run_i;

	/* Current index */

	size_t i;
} irs_iterator;

static irs_iterator irs_begin(struct isolating_run_sequence_s *seq)
{
	irs_iterator iter;

	iter.seq=seq;
	iter.level_run_i=0;

	/* Edge case, empty isolating run sequence */

	while (iter.level_run_i < seq->runs.n_level_runs)
	{
		iter.i=seq->runs.runs[iter.level_run_i].start;

		if (iter.i < seq->runs.runs[iter.level_run_i].end)
			break;

		++iter.level_run_i;
	}
	return iter;
}

static irs_iterator irs_end(struct isolating_run_sequence_s *seq)
{
	irs_iterator iter;

	iter.seq=seq;
	iter.level_run_i=seq->runs.n_level_runs;
	return iter;
}

static int irs_compare(const irs_iterator *a,
		       const irs_iterator *b)
{
	if (a->level_run_i < b->level_run_i)
		return -1;
	if (a->level_run_i > b->level_run_i)
		return 1;

	if (a->level_run_i == a->seq->runs.n_level_runs)
		return 0;

	if (a->i < b->i)
		return -1;

	if (a->i > b->i)
		return 1;
	return 0;
}

static void irs_incr(irs_iterator *iter)
{
	if (iter->seq->runs.n_level_runs == iter->level_run_i)
	{
		fprintf(stderr, "%s%s\n",
			"Internal error: attempting to increment ",
			"one past end of isolating run sequence iterator");
		abort();
	}

	if (++iter->i >= iter->seq->runs.runs[iter->level_run_i].end)
	{
		if (++iter->level_run_i < iter->seq->runs.n_level_runs)
			iter->i=iter->seq->runs.runs[iter->level_run_i].start;
	}
}

static void irs_decr(irs_iterator *iter)
{
	while (1)
	{
		if (iter->seq->runs.n_level_runs > iter->level_run_i &&
		    iter->i > iter->seq->runs.runs[iter->level_run_i].start)
		{
			--iter->i;
			break;
		}

		if (iter->level_run_i == 0)
		{
			fprintf(stderr, "%s%s\n",
				"Internal error: attempting to decrement the ",
				"beginning isolating run sequence iterator");
			abort();
		}

		iter->i=iter->seq->runs.runs[--iter->level_run_i].end;
	}
}

/* Used to build isolating run sequences link */

struct isolating_run_sequence_link {
	size_t push;
	size_t pop;
	struct isolating_run_sequence_s *seq;
};

/* Our isolating run sequences */

struct isolating_run_sequences_s {
	struct isolating_run_sequence_s *head, *tail; /* Linked list */

	/* Links LRI and RLI to its matching PDI */

	struct isolating_run_sequence_link *pdi_linkage; /* Recycle level_run */
	size_t n_pdi_linkage;
	size_t cap_pdi_linkage;
};

static int compare_irs_by_push(struct isolating_run_sequence_link **a,
			       struct isolating_run_sequence_link **b)
{
	if ((*a)->push < (*b)->push)
		return -1;
	if ((*a)->push > (*b)->push)
		return 1;
	return 0;
}

static struct isolating_run_sequence_link **sort_irs_links_by_push
(struct isolating_run_sequences_s *seq)
{
	struct isolating_run_sequence_link **p;
	size_t i;

	if (seq->n_pdi_linkage == 0)
		return 0;

	p=(struct isolating_run_sequence_link **)
		calloc(sizeof(struct isolating_run_sequence_link *),
		       seq->n_pdi_linkage);

	for (i=0; i<seq->n_pdi_linkage; i++)
	{
		p[i]=seq->pdi_linkage+i;
	}
	qsort(p, seq->n_pdi_linkage, sizeof(*p),
	      (int (*)(const void *, const void *))compare_irs_by_push);
	return p;
}

static 	struct isolating_run_sequence_s *
isolating_run_sequences_init(struct isolating_run_sequences_s *p,
			     unicode_bidi_level_t embedding_level,
			     size_t i)
{
	struct isolating_run_sequence_s *seq=
		(struct isolating_run_sequence_s *)
		calloc(1, sizeof(struct isolating_run_sequence_s));

	if (!seq) abort();

	level_runs_init(&seq->runs);

	struct level_run *run=level_runs_add(&seq->runs);

	run->start=i;
	run->end=i;
	seq->embedding_level=embedding_level;

	if (!p->head)
	{
		p->head=p->tail=seq;
	}
	else
	{
		p->tail->next=seq;
		seq->prev=p->tail;
		p->tail=seq;
	}

	return seq;
}

static void isolating_run_sequences_record(struct isolating_run_sequence_s *p,
					   size_t i)
{
	struct level_run *current_level_run=
		&p->runs.runs[p->runs.n_level_runs-1];

	if (current_level_run->start == current_level_run->end)
	{
		current_level_run->start=i;
		current_level_run->end=i+1;
		return;
	}

	if (current_level_run->end == i)
	{
		++current_level_run->end;
		return;
	}

	/*
	** Starting a new level run in the current isolating
	** run sequence.
	*/

	current_level_run=level_runs_add(&p->runs);

	current_level_run->start=i;
	current_level_run->end=i+1;
}

static void isolating_run_sequence_link(struct isolating_run_sequences_s *p,
					size_t push,
					size_t pop)
{
	if (p->n_pdi_linkage == p->cap_pdi_linkage)
	{
		p->cap_pdi_linkage=
			p->cap_pdi_linkage == 0
			? 1 : p->cap_pdi_linkage*2;

		p->pdi_linkage=(struct isolating_run_sequence_link *)
			(p->pdi_linkage ?
			 realloc(p->pdi_linkage,
				 p->cap_pdi_linkage * sizeof(*p->pdi_linkage))
			 :
			 malloc(p->cap_pdi_linkage * sizeof(*p->pdi_linkage)));

		if (!p->pdi_linkage)
			abort();
	}

	p->pdi_linkage[p->n_pdi_linkage].push=push;
	p->pdi_linkage[p->n_pdi_linkage].pop=pop;
	p->pdi_linkage[p->n_pdi_linkage].seq=0;
	++p->n_pdi_linkage;
}

static void isolating_run_sequences_deinit(struct isolating_run_sequences_s *p)
{
	struct isolating_run_sequence_s *seq;

	for (seq=p->head; seq; )
	{
		struct isolating_run_sequence_s *p=seq;

		seq=seq->next;

		level_runs_deinit(&p->runs);
		free(p);
	}

	if (p->pdi_linkage)
		free(p->pdi_linkage);
}

/* An entry on the directional status stack, used by the X rules */

struct directional_status_stack_entry {
	struct directional_status_stack_entry	*next;
	unicode_bidi_level_t			embedding_level;
	directional_override_status_t		directional_override_status;
	int					directional_isolate_status;
	size_t					isolate_start;
};

typedef struct {
	struct directional_status_stack_entry *head;

	struct unicode_bidi_direction paragraph_embedding_level;
	const char32_t    *chars;
	enum_bidi_type_t *types;
	const enum_bidi_type_t *orig_types;
	unicode_bidi_level_t *levels;
	size_t size;
	int overflow_isolate_count;
	int overflow_embedding_count;
	int valid_isolate_count;

	struct isolating_run_sequences_s isolating_run_sequences;
} *directional_status_stack_t;

#ifdef BIDI_DEBUG

static const struct {
	char			classname[8];
	enum_bidi_type_t	classenum;
} bidiclassnames[]={

#include "bidi_classnames.h"

};

const char *bidi_classname(enum_bidi_type_t classenum)
{
	for (const auto &cn:bidiclassnames)
	{
		if (cn.classenum == classenum)
			return cn.classname;
	}

	return "???";
}


void dump_types(const char *prefix, directional_status_stack_t stack)
{
	fprintf(DEBUGDUMP, "%s: ", prefix);
	for (size_t i=0; i<stack->size; ++i)
	{
		fprintf(DEBUGDUMP, " %s(%d)",
			bidi_classname(stack->types[i]),
			(int)stack->levels[i]);
	}
	fprintf(DEBUGDUMP, "\n");
}

void dump_orig_types(const char *prefix, directional_status_stack_t stack)
{
	fprintf(DEBUGDUMP, "%s: ", prefix);

	for (size_t i=0; i<stack->size; ++i)
	{
		fprintf(DEBUGDUMP, " %s(%s%s%d)",
			bidi_classname(stack->types[i]),
			(stack->types[i] != stack->orig_types[i] ?
			 bidi_classname(stack->orig_types[i]):""),
			(stack->types[i] != stack->orig_types[i] ? "/":""),
			(int)stack->levels[i]);
	}
	fprintf(DEBUGDUMP, "\n");
}
#endif


static void directional_status_stack_push
(directional_status_stack_t stack,
 unicode_bidi_level_t			embedding_level,
 directional_override_status_t		directional_override_status,
 int					directional_isolate_status)
{
	struct directional_status_stack_entry *p=
		(struct directional_status_stack_entry *)
		malloc(sizeof(struct directional_status_stack_entry));

	if (!p)
		abort();
#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "BIDI: Push level %d, override: %s, isolate: %s\n",
		(int)embedding_level,
		(directional_override_status == do_right_to_left ? "RTOL":
		 directional_override_status == do_left_to_right ? "LTOR":
		 directional_override_status == do_neutral ? "NEUTRAL"
		 : "ERROR"),
		(directional_isolate_status ? "YES":"NO"));

#endif

	p->embedding_level=embedding_level;
	p->directional_override_status=directional_override_status;
	p->directional_isolate_status=directional_isolate_status;
	p->next=stack->head;
	stack->head=p;
}

static struct unicode_bidi_direction
compute_paragraph_embedding_level(size_t i, size_t j,
				  enum_bidi_type_t (*get)(size_t i,
							  void *arg),
				  void *arg)
{
	struct unicode_bidi_direction ret;

	memset(&ret, 0, sizeof(ret));
	ret.direction=UNICODE_BIDI_LR;

	unicode_bidi_level_t in_isolation=0;

	for (; i<j; ++i)
	{
		enum_bidi_type_t t=get(i, arg);

		if (is_isolate_initiator(t))
			++in_isolation;
		else if (t == UNICODE_BIDI_TYPE_PDI)
		{
			if (in_isolation)
				--in_isolation;
		}

		if (in_isolation == 0)
		{
			if (t == UNICODE_BIDI_TYPE_AL ||
			    t == UNICODE_BIDI_TYPE_R)
			{
				ret.direction=UNICODE_BIDI_RL;
				ret.is_explicit=1;
				break;
			}
			if (t == UNICODE_BIDI_TYPE_L)
			{
				ret.is_explicit=1;
				break;
			}
		}
	}
	return ret;
}

struct compute_paragraph_embedding_level_type_info {
	const enum_bidi_type_t *p;
};

static enum_bidi_type_t
get_enum_bidi_type_for_paragraph_embedding_level(size_t i,
						 void *arg)
{
	struct compute_paragraph_embedding_level_type_info *p=
		(struct compute_paragraph_embedding_level_type_info *)arg;

	return p->p[i];
}

static struct unicode_bidi_direction
compute_paragraph_embedding_level_from_types(const enum_bidi_type_t *p,
					     size_t i, size_t j)
{
	struct compute_paragraph_embedding_level_type_info info;
	info.p=p;

	return compute_paragraph_embedding_level
		(i, j,
		 get_enum_bidi_type_for_paragraph_embedding_level,
		 &info);
}

static directional_status_stack_t
directional_status_stack_init(const char32_t *chars,
			      const enum_bidi_type_t *types, size_t n,
			      unicode_bidi_level_t *levels,
			      const unicode_bidi_level_t
			      *initial_embedding_level)
{
	directional_status_stack_t stack;

	stack=(directional_status_stack_t)calloc(1, sizeof(*stack));

	if (initial_embedding_level)
	{
		stack->paragraph_embedding_level.direction=
			*initial_embedding_level & 1;
		stack->paragraph_embedding_level.is_explicit=1;
	}
	else
	{
		stack->paragraph_embedding_level=
			compute_paragraph_embedding_level_from_types(types,
								     0, n);
	}
	stack->chars=chars;
	stack->orig_types=types;

	if (n)
	{
		stack->types=(enum_bidi_type_t *)
			malloc(sizeof(enum_bidi_type_t)*n);
		if (!stack->types)
			abort();
		memcpy(stack->types, stack->orig_types,
		       sizeof(enum_bidi_type_t)*n);
	}
	else
	{
		stack->types=0;
	}
	stack->levels=levels;
	stack->size=n;

	directional_status_stack_push(stack,
				      stack->paragraph_embedding_level
				      .direction,
				      do_neutral, 0);

	return stack;
}

static void directional_status_stack_pop(directional_status_stack_t stack)
{
	struct directional_status_stack_entry *head=stack->head;

	if (!head)
		return;

	stack->head=head->next;
	free(head);

#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "BIDI: Pop\n");
#endif
}

static void directional_status_stack_deinit(directional_status_stack_t stack)
{
	while (stack->head)
		directional_status_stack_pop(stack);
	if (stack->types)
		free(stack->types);
	isolating_run_sequences_deinit(&stack->isolating_run_sequences);
	free(stack);
}

enum_bidi_type_t unicode_bidi_type(char32_t c)
{
	return (enum_bidi_type_t)
		unicode_tab_lookup(c,
				   unicode_starting_indextab,
				   unicode_starting_pagetab,
				   sizeof(unicode_starting_indextab)/
				   sizeof(unicode_starting_indextab[0]),
				   unicode_rangetab,
				   sizeof(unicode_rangetab)/
				   sizeof(unicode_rangetab[0]),
				   unicode_classtab,
				   UNICODE_BIDI_TYPE_L);
}


void unicode_bidi_calc_types(const char32_t *p, size_t n,
			     enum_bidi_type_t *buf)
{
	/*
	** Look up the bidi class for each char32_t.
	*/
	for (size_t i=0; i<n; ++i)
	{
		buf[i]=unicode_bidi_type(p[i]);
#ifdef UNICODE_BIDI_TEST
		UNICODE_BIDI_TEST(i);
#endif
	}
}

void unicode_bidi_setbnl(char32_t *p,
			 const enum_bidi_type_t *types,
			 size_t n)
{
	for (size_t i=0; i<n; i++)
		if (types[i] == UNICODE_BIDI_TYPE_B)
		{
			p[i]='\n';
		}
}

struct unicode_bidi_direction
unicode_bidi_calc(const char32_t *p, size_t n, unicode_bidi_level_t *bufp,
		  const unicode_bidi_level_t *initial_embedding_level)
{
	enum_bidi_type_t *buf=
		(enum_bidi_type_t *)malloc(n * sizeof(enum_bidi_type_t));

	if (!buf)
		abort();

	unicode_bidi_calc_types(p, n, buf);

	struct unicode_bidi_direction level=
		unicode_bidi_calc_levels(p,
					 buf,
					 n,
					 bufp,
					 initial_embedding_level);

	free(buf);

	return level;
}

static void unicode_bidi_cl(directional_status_stack_t stack);

struct unicode_bidi_direction
unicode_bidi_calc_levels(const char32_t *p,
			 const enum_bidi_type_t *types,
			 size_t n,
			 unicode_bidi_level_t *bufp,
			 const unicode_bidi_level_t *initial_embedding_level)
{
	directional_status_stack_t stack;

	for (size_t i=0; i<n; ++i)
	{
		bufp[i]=UNICODE_BIDI_SKIP;
	}

	stack=directional_status_stack_init(p, types, n, bufp,
					    initial_embedding_level);

	struct unicode_bidi_direction paragraph_embedding_level=
		stack->paragraph_embedding_level;

#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "BIDI: START: Paragraph embedding level: %d\n",
		(int)paragraph_embedding_level.direction);
#endif

	unicode_bidi_cl(stack);

	directional_status_stack_deinit(stack);

	return paragraph_embedding_level;
}

#define RESET_CLASS(p,stack) do {				\
		switch ((stack)->head->directional_override_status) {	\
		case do_neutral: break;					\
		case do_left_to_right: (p)=UNICODE_BIDI_TYPE_L; break; \
		case do_right_to_left: (p)=UNICODE_BIDI_TYPE_R; break; \
		}							\
	} while(0)

static void unicode_bidi_w(enum_bidi_type_t *types,
			   struct isolating_run_sequence_s *seq);
static void unicode_bidi_n(directional_status_stack_t stack,
			   struct isolating_run_sequence_s *seq);

#ifdef BIDI_DEBUG
void dump_sequence_info(directional_status_stack_t stack,
			struct isolating_run_sequence_s *seq)
{
	fprintf(DEBUGDUMP, "Sequence: sos: %c, eos: %c:",
		(seq->sos == UNICODE_BIDI_TYPE_L ? 'L':'R'),
		(seq->eos == UNICODE_BIDI_TYPE_L ? 'L':'R'));

	for (size_t i=0; i<seq->runs.n_level_runs; ++i)
	{
		fprintf(DEBUGDUMP, "%s[%lu-%lu]",
			i == 0 ? " ":", ",
			(unsigned long)seq->runs.runs[i].start,
			(unsigned long)seq->runs.runs[i].end-1);
	}
	fprintf(DEBUGDUMP, "\n");
}

void dump_sequence(const char *what, directional_status_stack_t stack,
		   struct isolating_run_sequence_s *seq)
{
	irs_iterator beg=irs_begin(seq), end=irs_end(seq);

	fprintf(DEBUGDUMP, "%s: ", what);
	while (irs_compare(&beg, &end))
	{
		fprintf(DEBUGDUMP, " %s(%d)",
			bidi_classname(stack->types[beg.i]),
			(int)stack->levels[beg.i]);
		irs_incr(&beg);
	}
	fprintf(DEBUGDUMP, "\n");
}
#endif

static void unicode_bidi_cl(directional_status_stack_t stack)
{
#ifdef BIDI_DEBUG
	dump_types("Before X1", stack);
#endif

	for (size_t i=0; i<stack->size; i++)
	{
		int embedding_level;

#define NEXT_ODD_EMBEDDING_LEVEL				\
		(embedding_level=stack->head->embedding_level,	\
		 ++embedding_level,				\
		 embedding_level |= 1)

#define NEXT_EVEN_EMBEDDING_LEVEL				\
		(embedding_level=stack->head->embedding_level,	\
		 embedding_level |= 1,				\
		 ++embedding_level)

		switch (stack->types[i]) {
		case UNICODE_BIDI_TYPE_RLE:
			/* X2 */
			NEXT_ODD_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				directional_status_stack_push
					(stack, embedding_level,
					 do_neutral, 0);
			}
			else
			{
				if (stack->overflow_isolate_count == 0)
				{
					++stack->overflow_embedding_count;
				}
			}
			break;
		case UNICODE_BIDI_TYPE_LRE:
			/* X3 */

			NEXT_EVEN_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				directional_status_stack_push
					(stack, embedding_level,
					 do_neutral, 0);
			}
			else
			{
				if (stack->overflow_isolate_count == 0)
				{
					++stack->overflow_embedding_count;
				}
			}
			break;

		case UNICODE_BIDI_TYPE_RLO:
			/* X4 */
			NEXT_ODD_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				directional_status_stack_push
					(stack, embedding_level,
					 do_right_to_left, 0);
			}
			else
			{
				if (stack->overflow_isolate_count == 0)
				{
					++stack->overflow_embedding_count;
				}
			}
			break;

		case UNICODE_BIDI_TYPE_LRO:
			/* X5 */
			NEXT_EVEN_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				directional_status_stack_push
					(stack, embedding_level,
					 do_left_to_right, 0);
			}
			else
			{
				if (stack->overflow_isolate_count == 0)
				{
					++stack->overflow_embedding_count;
				}
			}
			break;
		default:
			break;
		}

		enum_bidi_type_t cur_class=stack->types[i];

		if (cur_class == UNICODE_BIDI_TYPE_FSI) {
			/* X5c */

			size_t j=i;

			unicode_bidi_level_t in_isolation=1;

			while (++j < stack->size)
			{
				if (is_isolate_initiator(stack->types[j]))
					++in_isolation;
				else if (stack->types[j] == UNICODE_BIDI_TYPE_PDI)
				{
					if (--in_isolation == 0)
						break;
				}
			}

			cur_class=compute_paragraph_embedding_level_from_types
				(stack->types, i+1, j).direction
				!= UNICODE_BIDI_LR
				? UNICODE_BIDI_TYPE_RLI
				: UNICODE_BIDI_TYPE_LRI;
		}

		switch (cur_class) {
		case UNICODE_BIDI_TYPE_RLI:
			/* X5a */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->types[i],stack);

			NEXT_ODD_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				++stack->valid_isolate_count;
				directional_status_stack_push
					(stack, embedding_level, do_neutral, 1);
				stack->head->isolate_start=i;
			}
			else
			{
				++stack->overflow_isolate_count;
			}
			break;

		case UNICODE_BIDI_TYPE_LRI:
			/* X5b */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->types[i],stack);

			NEXT_EVEN_EMBEDDING_LEVEL;

			if (embedding_level <= max_depth &&
			    stack->overflow_isolate_count == 0 &&
			    stack->overflow_embedding_count == 0)
			{
				++stack->valid_isolate_count;
				directional_status_stack_push
					(stack, embedding_level, do_neutral, 1);
				stack->head->isolate_start=i;
			}
			else
			{
				++stack->overflow_isolate_count;
			}
			break;

		default:
			break;
		}

		if (!is_explicit_indicator(stack->orig_types[i]))
		{
			/* X6 */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->types[i],stack);
		}

		if (stack->types[i] == UNICODE_BIDI_TYPE_PDI)
		{
			/* X6a */
			if (stack->overflow_isolate_count > 0)
			{
				--stack->overflow_isolate_count;
			}
			else if (stack->valid_isolate_count == 0)
				;
			else
			{
				stack->overflow_embedding_count=0;

				while (!stack->head->directional_isolate_status)
				{
					directional_status_stack_pop(stack);

					if (!stack->head)
					{
						fprintf(stderr,
							"Internal error: stack "
							"empty after pop\n");
						abort();
					}
				}

				isolating_run_sequence_link
					(&stack->isolating_run_sequences,
					 stack->head->isolate_start,
					 i);

				directional_status_stack_pop(stack);
				--stack->valid_isolate_count;

				if (!stack->head)
				{
					fprintf(stderr,
						"Internal error: stack "
						"empty after pop\n");
					abort();
				}
			}
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->types[i],stack);
		}

		if (stack->types[i] == UNICODE_BIDI_TYPE_PDF)
		{
			/* X7 */

			if (stack->overflow_isolate_count > 0)
				;
			else if (stack->overflow_embedding_count > 0)
			{
				--stack->overflow_embedding_count;
			}
			else
			{
				stack->overflow_embedding_count=0;

				if (!stack->head->directional_isolate_status
				    && stack->head->next)
				{
					directional_status_stack_pop(stack);
				}
			}
		}

		if (stack->types[i] == UNICODE_BIDI_TYPE_B)
		{
			/* X8 */

			stack->levels[i]=
				stack->paragraph_embedding_level.direction;
		}
	}

	/* X9 */

#define IS_X9(class)						\
		((class) == UNICODE_BIDI_TYPE_RLE ||		\
		 (class) == UNICODE_BIDI_TYPE_LRE ||		\
		 (class) == UNICODE_BIDI_TYPE_RLO ||		\
		 (class) == UNICODE_BIDI_TYPE_LRO ||		\
		 (class) == UNICODE_BIDI_TYPE_PDF ||		\
		 (class) == UNICODE_BIDI_TYPE_BN)

	size_t next_pdi=0;
	struct isolating_run_sequence_s *current_irs=0;
	unicode_bidi_level_t current_level=0;

	struct isolating_run_sequence_link **push_links=
		sort_irs_links_by_push(&stack->isolating_run_sequences);
	size_t next_rlilri=0;

	for (size_t i=0; i<stack->size; ++i)
	{
		if (IS_X9(stack->types[i]))
		{
			if (stack->levels[i] != UNICODE_BIDI_SKIP)
			{
				fprintf(stderr, "Internal error: X9 class did "
					"not get skipped");
				abort();
			}
			continue;
		}

		if (stack->levels[i] == UNICODE_BIDI_SKIP)
		{
			fprintf(stderr, "Internal error: non-X9 class did "
				"get skipped for some reason");
			abort();
		}

		if (!current_irs || current_level != stack->levels[i])
		{
			current_level = stack->levels[i];

			if (next_pdi <
			    stack->isolating_run_sequences.n_pdi_linkage &&
			    stack->isolating_run_sequences.pdi_linkage
			    [next_pdi].pop == i)
			{
				if ((current_irs=
				     stack->isolating_run_sequences.pdi_linkage
				     [next_pdi].seq) == 0)
				{
					fprintf(stderr, "Internal error: "
						"missing previous isolated "
						"run sequence link\n");
					abort();
				}
				++next_pdi;
			}
			else
			{
				current_irs=isolating_run_sequences_init
					(&stack->isolating_run_sequences,
					 stack->levels[i],
					 i);
			}
		}

		isolating_run_sequences_record(current_irs, i);

		if (next_rlilri < stack->isolating_run_sequences.n_pdi_linkage &&
		    push_links[next_rlilri]->push == i)
		{
			push_links[next_rlilri++]->seq=current_irs;
		}
	}

	if (push_links)
		free(push_links);

	/* X10 */
#ifdef BIDI_DEBUG
	dump_types("Before X10", stack);
#endif

	for (struct isolating_run_sequence_s *p=
		     stack->isolating_run_sequences.head; p; p=p->next)
	{
		p->sos=p->eos=UNICODE_BIDI_TYPE_L;

		irs_iterator beg_iter=irs_begin(p), end_iter=irs_end(p);

		if (irs_compare(&beg_iter, &end_iter) == 0)
			continue; /* Edge case */

		unicode_bidi_level_t before=
			stack->paragraph_embedding_level.direction;
		unicode_bidi_level_t after=
			stack->paragraph_embedding_level.direction;


		size_t first_i=beg_iter.i;

		irs_decr(&end_iter);

		size_t end_i=end_iter.i+1;

		unicode_bidi_level_t start=stack->levels[first_i];
		unicode_bidi_level_t end=stack->levels[end_i-1];

		while (first_i > 0 && stack->levels[first_i-1] ==
		       UNICODE_BIDI_SKIP)
			--first_i;

		if (first_i > 0)
			before=stack->levels[first_i-1];

		if (!is_isolate_initiator(stack->types[end_iter.i]))
		{
			while (end_i < stack->size &&
			       stack->levels[end_i] == UNICODE_BIDI_SKIP)
				++end_i;

			if (end_i < stack->size)
				after=stack->levels[end_i];
		}
#ifdef BIDI_DEBUG
		fprintf(DEBUGDUMP,
			"Sequence: before=%d, start=%d, end=%d, after=%d\n",
			(int)before, (int)start, (int)end, (int)after);
#endif
		if (start > before)
			before=start;

		if (end > after)
			after=end;

		if (before & 1)
			p->sos=UNICODE_BIDI_TYPE_R;
		if (after & 1)
			p->eos=UNICODE_BIDI_TYPE_R;


#ifdef BIDI_DEBUG
		dump_sequence_info(stack, p);
		dump_sequence("Contents", stack, p);
#endif
	}


	for (struct isolating_run_sequence_s *p=
		     stack->isolating_run_sequences.head; p; p=p->next)
	{
#ifdef BIDI_DEBUG
		dump_sequence_info(stack, p);
		fprintf(DEBUGDUMP, "Sequence embedding level: %d\n",
			(int)p->embedding_level);
		dump_sequence("Contents before W", stack, p);
#endif

		unicode_bidi_w(stack->types, p);

#ifdef BIDI_DEBUG
		dump_sequence("Contents after W", stack, p);
#endif
		unicode_bidi_n(stack, p);
	}
#ifdef BIDI_DEBUG
	dump_orig_types("Before L1", stack);
#endif

	/*
	** L1
	*/

	size_t i=stack->size;

	int seen_sb=1;

	while (i > 0)
	{
		--i;

		if (IS_X9(stack->orig_types[i]))
			continue;

		switch (stack->orig_types[i]) {
		case UNICODE_BIDI_TYPE_WS:
		case UNICODE_BIDI_TYPE_FSI:
		case UNICODE_BIDI_TYPE_LRI:
		case UNICODE_BIDI_TYPE_RLI:
		case UNICODE_BIDI_TYPE_PDI:
			if (seen_sb)
				stack->levels[i]=
					stack->paragraph_embedding_level.direction;
			break;
		case UNICODE_BIDI_TYPE_S:
		case UNICODE_BIDI_TYPE_B:
			stack->levels[i]=stack->paragraph_embedding_level.direction;
			seen_sb=1;
			break;
		default:
			seen_sb=0;
			continue;
		}
	}
}

static void unicode_bidi_w(enum_bidi_type_t *types,
			   struct isolating_run_sequence_s *seq)
{
	irs_iterator iter=irs_begin(seq), end=irs_end(seq);
	enum_bidi_type_t previous_type=seq->sos;

	enum_bidi_type_t strong_type=UNICODE_BIDI_TYPE_R;

	while (irs_compare(&iter, &end))
	{
		if (types[iter.i] == UNICODE_BIDI_TYPE_NSM)
		{
			/* W1 */
			types[iter.i] =
				is_isolate_initiator(previous_type) ||
				previous_type == UNICODE_BIDI_TYPE_PDI
				? UNICODE_BIDI_TYPE_ON
				: previous_type;

		}

		/* W2 */

		if (types[iter.i] == UNICODE_BIDI_TYPE_EN &&
		    strong_type == UNICODE_BIDI_TYPE_AL)
		{
			types[iter.i] = UNICODE_BIDI_TYPE_AN;
		}

		/* W2 */
		previous_type=types[iter.i];

		switch (previous_type) {
		case UNICODE_BIDI_TYPE_R:
		case UNICODE_BIDI_TYPE_L:
		case UNICODE_BIDI_TYPE_AL:
			strong_type=previous_type;
			break;
		default:
			break;
		}

		irs_incr(&iter);
	}

	iter=irs_begin(seq);

	previous_type=UNICODE_BIDI_TYPE_L;

	int not_eol=irs_compare(&iter, &end);

	while (not_eol)
	{
		/* W3 */
		if (types[iter.i] == UNICODE_BIDI_TYPE_AL)
			types[iter.i] = UNICODE_BIDI_TYPE_R;

		/* W4 */

		enum_bidi_type_t this_type=types[iter.i];
		irs_incr(&iter);

		not_eol=irs_compare(&iter, &end);

		if (not_eol &&
		    (
		     (this_type == UNICODE_BIDI_TYPE_ES &&
		      previous_type == UNICODE_BIDI_TYPE_EN)
		     ||
		     (this_type == UNICODE_BIDI_TYPE_CS &&
		      (previous_type == UNICODE_BIDI_TYPE_EN ||
		       previous_type == UNICODE_BIDI_TYPE_AN)
		      )
		     ) &&
		    types[iter.i] == previous_type)
		{
			irs_iterator prev=iter;

			irs_decr(&prev);

			types[prev.i]=previous_type;
		}

		if (not_eol)
			previous_type=this_type;
	}

	iter=irs_begin(seq);

	/* W5 */

	previous_type=UNICODE_BIDI_TYPE_L; /* Doesn't match any part of W5 */

	while (irs_compare(&iter, &end))
	{
		if (types[iter.i] != UNICODE_BIDI_TYPE_ET)
		{
			previous_type=types[iter.i];
			irs_incr(&iter);
			continue;
		}

		/* ET after EN */
		if (previous_type == UNICODE_BIDI_TYPE_EN)
		{
			types[iter.i] = UNICODE_BIDI_TYPE_EN;
			irs_incr(&iter);
			continue;
		}

		/* See if EN follows these ETs */

		irs_iterator start=iter;

		while (irs_incr(&iter), irs_compare(&iter, &end))
		{
			previous_type=types[iter.i];

			if (previous_type == UNICODE_BIDI_TYPE_ET)
				continue;

			if (previous_type == UNICODE_BIDI_TYPE_EN)
			{
				while (irs_compare(&start, &iter))
				{
					types[start.i]=
						UNICODE_BIDI_TYPE_EN;
					irs_incr(&start);
				}
			}
			break;
		}
	}

	/* W6 */

	for (iter=irs_begin(seq);
	     irs_compare(&iter, &end); irs_incr(&iter))
	{
		switch (types[iter.i]) {
		case UNICODE_BIDI_TYPE_ET:
		case UNICODE_BIDI_TYPE_ES:
		case UNICODE_BIDI_TYPE_CS:
			/* W6 */
			types[iter.i]=UNICODE_BIDI_TYPE_ON;
			break;
		default:
			break;
		}
	}

	/* W7 */
	iter=irs_begin(seq);

	previous_type=seq->sos;

	while (irs_compare(&iter, &end))
	{
		switch (types[iter.i]) {
		case UNICODE_BIDI_TYPE_L:
		case UNICODE_BIDI_TYPE_R:
			previous_type=types[iter.i];
			break;
		case UNICODE_BIDI_TYPE_EN:
			if (previous_type == UNICODE_BIDI_TYPE_L)
				types[iter.i]=previous_type;
			break;
		default:
			break;
		}
		irs_incr(&iter);
	}
}

struct bidi_n_stack {
	struct bidi_n_stack *next;

	irs_iterator start;
	irs_iterator end;
	short has_e;
	short has_o;
	short matched;
};

#define IS_NI(class)						\
	((class) == UNICODE_BIDI_TYPE_B ||			\
	 (class) == UNICODE_BIDI_TYPE_S ||			\
	 (class) == UNICODE_BIDI_TYPE_WS ||			\
	 (class) == UNICODE_BIDI_TYPE_ON ||			\
	 (class) == UNICODE_BIDI_TYPE_FSI ||			\
	 (class) == UNICODE_BIDI_TYPE_LRI ||			\
	 (class) == UNICODE_BIDI_TYPE_RLI ||			\
	 (class) == UNICODE_BIDI_TYPE_PDI)

static void unicode_bidi_n(directional_status_stack_t stack,
			   struct isolating_run_sequence_s *seq)
{
#define NSTACKSIZE 63

	struct bidi_n_stack *bracket_stack=0;
	struct bidi_n_stack **bracket_stack_tail= &bracket_stack;

	struct bidi_n_stack *stack_iters[NSTACKSIZE];
	char32_t stack_chars[NSTACKSIZE];
	size_t stackp=0;

	/*
	** N0
	**
	** Combined pass that implements BD16, and constructs, on the fly
	** whether each matched pair has an e or an o inside it.
	*/
	irs_iterator beg=irs_begin(seq), iter=beg, end=irs_end(seq);

	for (; irs_compare(&iter, &end); irs_incr(&iter))
	{
		unicode_bidi_bracket_type_t bracket_type=UNICODE_BIDI_n;

		char32_t open_or_close_bracket=0;

		if (IS_NI(stack->types[iter.i]))
		{
			open_or_close_bracket=
				unicode_bidi_bracket_type(stack->chars[iter.i],
							  &bracket_type);
		}

		if (bracket_type == UNICODE_BIDI_o)
		{
			if (stackp >= NSTACKSIZE)
			{
#ifdef BIDI_DEBUG
				fprintf(DEBUGDUMP,
					"BD16 stack exceeded on index %d\n",
					(int)iter.i);
#endif
				break; /* BD16 failure */
			}
			if (!((*bracket_stack_tail)=(struct bidi_n_stack *)
			      calloc(1, sizeof(struct bidi_n_stack))))
				abort();

			stack_iters[stackp]=*bracket_stack_tail;
			stack_iters[stackp]->start=iter;

			stack_chars[stackp]=stack->chars[iter.i];

			unicode_canonical_t canon=
				unicode_canonical(stack_chars[stackp]);

			if (canon.n_canonical_chars == 1 &&
			    !canon.format)
			{
				stack_chars[stackp]=
					canon.canonical_chars[0];
			}

			bracket_stack_tail= &(*bracket_stack_tail)->next;
			++stackp;
#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP, "Found opening bracket at index %d\n",
				(int)iter.i);
#endif
		}

		if (bracket_type == UNICODE_BIDI_c)
		{
			unicode_canonical_t canon=
				unicode_canonical(open_or_close_bracket);

			if (canon.n_canonical_chars == 1 &&
			    !canon.format)
			{
				open_or_close_bracket=
					canon.canonical_chars[0];
			}
#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP, "Found closing bracket at index %d\n",
				(int)iter.i);
#endif
			for (size_t i=stackp; i > 0; )
			{
				--i;
				if (stack_chars[i] != open_or_close_bracket)
					continue;
#ifdef BIDI_DEBUG
				fprintf(DEBUGDUMP,
					"Matched to open bracket at index %d\n",
					(int)stack_iters[i]->start.i);
#endif

				stack_iters[i]->end = iter;
				stack_iters[i]->matched=1;
				stackp=i;
				break;
			}
		}

		/*
		** So we check if this is an e or an o, and if so we
		** have a convenient list of all unclosed brackets, so
		** we record these facts there.
		*/

		enum_bidi_type_t eoclass=stack->types[iter.i];

#define ADJUST_EOCLASS(eoclass) do {					\
									\
			if ((eoclass) == UNICODE_BIDI_TYPE_EN ||	\
			    (eoclass) == UNICODE_BIDI_TYPE_AN)		\
				(eoclass)=UNICODE_BIDI_TYPE_R;		\
		} while (0)

		ADJUST_EOCLASS(eoclass);

#define E_CLASS(level) ((level) & 1 ?					\
			UNICODE_BIDI_TYPE_R:UNICODE_BIDI_TYPE_L)

#define O_CLASS(level) ((level) & 1 ?					\
			UNICODE_BIDI_TYPE_L:UNICODE_BIDI_TYPE_R)

		if (eoclass == E_CLASS(seq->embedding_level))
		{
#ifdef BIDI_DEBUG
			if (stackp)
			{
				fprintf(DEBUGDUMP,
					"Found e for brackets at:");

				for (size_t i=0; i<stackp; ++i)
				{
					fprintf(DEBUGDUMP,
						" %d",
						(int)stack_iters[i]->start.i);
				}
				fprintf(DEBUGDUMP, "\n");
			}
#endif
			for (size_t i=0; i<stackp; ++i)
				stack_iters[i]->has_e=1;
		}
		else if (eoclass == O_CLASS(seq->embedding_level))
		{
#ifdef BIDI_DEBUG
			if (stackp)
			{
				fprintf(DEBUGDUMP,
					"Found o for brackets at:");

				for (size_t i=0; i<stackp; ++i)
				{
					fprintf(DEBUGDUMP,
						" %d",
						(int)stack_iters[i]->start.i);
				}
				fprintf(DEBUGDUMP, "\n");
			}
#endif
			for (size_t i=0; i<stackp; ++i)
				stack_iters[i]->has_o=1;
		}
	}

	while (bracket_stack)
	{
		struct bidi_n_stack *p=bracket_stack;

		bracket_stack=bracket_stack->next;

		if (p->matched)
		{
			int set=0;

#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP,
				"Brackets: %d and %d: e=%s, o=%s",
				(int)p->start.i,
				(int)p->end.i,
				bidi_classname(E_CLASS(seq->embedding_level)),
				bidi_classname(O_CLASS(seq->embedding_level)));

			fprintf(DEBUGDUMP, ", has e=%d, has o=%d\n",
				p->has_e,
				p->has_o);
#endif
			if (p->has_e)
			{
				stack->types[p->start.i]=
					stack->types[p->end.i]=
					seq->embedding_level & 1
					? UNICODE_BIDI_TYPE_R
					: UNICODE_BIDI_TYPE_L;
				set=1;
			} else if (p->has_o)
			{
				enum_bidi_type_t strong_type=seq->sos;

				iter=p->start;

				while (irs_compare(&beg, &iter))
				{
					irs_decr(&iter);

					enum_bidi_type_t eoclass=
						stack->types[iter.i];

					ADJUST_EOCLASS(eoclass);

					switch (eoclass) {
					case UNICODE_BIDI_TYPE_L:
					case UNICODE_BIDI_TYPE_R:
						break;
					default:
						continue;
					}

					strong_type=eoclass;
#ifdef BIDI_DEBUG
					fprintf(DEBUGDUMP,
						"Brackets: O context: %s\n",
						bidi_classname(strong_type));
#endif
					break;
				}

				stack->types[p->start.i]=
					stack->types[p->end.i]=
					strong_type;
				set=1;
			}

			if (set)
			{
				enum_bidi_type_t strong_type=
					stack->types[p->end.i];

				while (irs_incr(&p->end),
				       irs_compare(&p->end, &end))
				{
					if (stack->orig_types[p->end.i] !=
					    UNICODE_BIDI_TYPE_NSM)
						break;

					stack->types[p->end.i]=strong_type;
				}
			}
		}
		free(p);
	}

	/* N1 */

	enum_bidi_type_t prev_type=seq->sos;

	for (iter=beg; irs_compare(&iter, &end); )
	{
		/*
		** N1
		*/

		enum_bidi_type_t this_type=stack->types[iter.i];

		ADJUST_EOCLASS(this_type);

		if (!IS_NI(this_type))
		{
			switch (this_type) {
			case UNICODE_BIDI_TYPE_L:
			case UNICODE_BIDI_TYPE_R:
				prev_type=this_type;
				break;
			default:
				prev_type=UNICODE_BIDI_TYPE_ON; // Marker.
				break;
			}
			irs_incr(&iter);
			continue;
		}

		enum_bidi_type_t next_type=seq->eos;

		irs_iterator start=iter;

		while (irs_compare(&iter, &end))
		{
			if (IS_NI(stack->types[iter.i]))
			{
				irs_incr(&iter);
				continue;
			}

			enum_bidi_type_t other_type=stack->types[iter.i];

			ADJUST_EOCLASS(other_type);

			switch (other_type) {
			case UNICODE_BIDI_TYPE_L:
			case UNICODE_BIDI_TYPE_R:
				next_type=other_type;
				break;
			default:
				next_type=UNICODE_BIDI_TYPE_BN; /* Marker */
				break;
			}
			break;
		}

		while (irs_compare(&start, &iter))
		{
			// next_type can only match prev_type if both are L
			// or R. If the character before the NIs was not L
			// or R, prev_type is ON. If the character after the
			// NIs was not L or R, next_type is BN.

			if (next_type == prev_type)
			{
				stack->types[start.i]=next_type; /* N1 */
			}

			irs_incr(&start);
		}
	}

	for (iter=beg; irs_compare(&iter, &end); )
	{
		if (IS_NI(stack->types[iter.i]))
		{
			stack->types[iter.i]=
				stack->levels[iter.i] & 1 ?
				UNICODE_BIDI_TYPE_R :
				UNICODE_BIDI_TYPE_L; /* N2 */
		}
		irs_incr(&iter);
	}

#ifdef BIDI_DEBUG
	dump_sequence("Contents after N", stack, seq);
#endif

	/* I1 */
	/* I2 */

	for (iter=beg; irs_compare(&iter, &end); irs_incr(&iter))
	{
		if ((stack->levels[iter.i] & 1) == 0)
		{
			switch (stack->types[iter.i]) {
			case UNICODE_BIDI_TYPE_R:
				++stack->levels[iter.i];
				break;
			case UNICODE_BIDI_TYPE_AN:
			case UNICODE_BIDI_TYPE_EN:
				stack->levels[iter.i] += 2;
				break;
			default: break;
			}
		}
		else
		{
			switch (stack->types[iter.i]) {
			case UNICODE_BIDI_TYPE_L:
			case UNICODE_BIDI_TYPE_AN:
			case UNICODE_BIDI_TYPE_EN:
				++stack->levels[iter.i];
				break;
			default: break;
			}
		}
	}

#ifdef BIDI_DEBUG
	dump_sequence("Contents after I", stack, seq);
#endif
}

struct level_run_layers {
	struct level_runs *lruns;     /* At this embedding level, or higher */
	size_t n_lruns;               /* How many of them */
	size_t cap_lruns;             /* Capacity of the level runs */
};

static void level_run_layers_init(struct level_run_layers *p)
{
	p->lruns=0;
	p->n_lruns=0;
	p->cap_lruns=0;
}

static void level_run_layers_deinit(struct level_run_layers *p)
{
	if (p->lruns)
	{
		for (size_t i=0; i<p->n_lruns; ++i)
			level_runs_deinit(&p->lruns[i]);
		free(p->lruns);
	}
}

static void level_run_layers_add(struct level_run_layers *p)
{
	if (p->n_lruns == p->cap_lruns)
	{
		p->cap_lruns *= 2;

		if (p->cap_lruns == 0)
			p->cap_lruns=1;

		p->lruns=(struct level_runs *)
			(p->lruns ?
			 realloc(p->lruns,
				 sizeof(struct level_runs) *
				 p->cap_lruns)
			 :malloc(sizeof(struct level_runs) *
				 p->cap_lruns));
		if (!p->lruns)
			abort();
	}

	level_runs_init(p->lruns + (p->n_lruns++));
}

static void reverse_str(char32_t *p,
			unicode_bidi_level_t *levels,
			size_t start,
			size_t end,
			void (*reorder_callback)(size_t, size_t, void *),
			void *arg)
{
	size_t right=end;
	size_t left=start;

	while (right > left)
	{
		--right;

		if (p)
		{
			char32_t c=p[left];
			unicode_bidi_level_t l=levels[left];

			p[left]=p[right];
			levels[left]=levels[right];
			p[right]=c;
			levels[right]=l;
		}
		++left;
	}

	if (end-start > 1 && reorder_callback)
		(*reorder_callback)(start, end-start, arg);
}

void unicode_bidi_reorder(char32_t *p,
			  unicode_bidi_level_t *levels,
			  size_t n,
			  void (*reorder_callback)(size_t, size_t, void *),
			  void *arg)
{
	/* L2 */

#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "Before L2:");
	for (size_t i=0; i<n; ++i)
		fprintf(DEBUGDUMP, " %04x/%d",
			(unsigned)p[i],
			(int)levels[i]);
	fprintf(DEBUGDUMP, "\n");
#endif

	struct level_run_layers layers;
	unicode_bidi_level_t previous_level=0;

	level_run_layers_init(&layers);

	for (size_t i=0; i<n; ++i)
	{
		if (levels[i] != UNICODE_BIDI_SKIP)
			previous_level=levels[i];

		while (layers.n_lruns <= previous_level)
			level_run_layers_add(&layers);

		/* We intentionally don't put anything in level 0 */
		for (size_t j=1; j<=previous_level; ++j)
		{
			struct level_runs *runs=layers.lruns+j;

			if (runs->n_level_runs &&
			    runs->runs[runs->n_level_runs-1].end == i)
			{
				++runs->runs[runs->n_level_runs-1].end;
			}
			else
			{
				struct level_run *run=
					level_runs_add(runs);

				run->start=i;
				run->end=i+1;
			}
		}
	}
#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "L2:\n");
#endif
	for (size_t i=layers.n_lruns; i; )
	{
		struct level_runs *runs=layers.lruns+ --i;

#ifdef BIDI_DEBUG
		if (runs->n_level_runs)
			fprintf(DEBUGDUMP, "Reverse %d:",
				(int)i);
#endif

		for (size_t j=0; j<runs->n_level_runs; ++j)
		{
			size_t start=runs->runs[j].start;
			size_t end=runs->runs[j].end;
#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP, " %d-%d",
				(int)start, (int)end-1);
#endif

			reverse_str(p, levels, start, end,
				    reorder_callback, arg);
		}

#ifdef BIDI_DEBUG
		if (runs->n_level_runs)
			fprintf(DEBUGDUMP, "\n");
#endif
	}

	level_run_layers_deinit(&layers);
}

static size_t unicode_bidi_count_or_cleanup(const char32_t *string,
					    char32_t *dest,
					    unicode_bidi_level_t *levels,
					    size_t n,
					    int cleanup_options,
					    void (*removed_callback)(size_t,
								     void *),
					    void *arg)
{
	size_t i=0;
	for (size_t j=0; j<n; ++j)
	{
		enum_bidi_type_t cl=unicode_bidi_type(string[j]);

		if (cleanup_options & UNICODE_BIDI_CLEANUP_EXTRA
		    ? (
		       is_explicit_indicator_except_b(cl) ||
		       (string[j] == UNICODE_LRM ||
			string[j] == UNICODE_RLM ||
			string[j] == UNICODE_ALM))
		    : IS_X9(cl))
		{
			if (removed_callback)
				(*removed_callback)(j, arg);
			continue;
		}
		if (levels)
			levels[i]=levels[j] & 1;

		if (dest)
			dest[i]=(cleanup_options & UNICODE_BIDI_CLEANUP_BNL)
				&& cl == UNICODE_BIDI_TYPE_B ? '\n' : string[j];
		++i;
	}
	return i;
}

size_t unicode_bidi_cleanup(char32_t *string,
			    unicode_bidi_level_t *levels,
			    size_t n,
			    int cleanup_options,
			    void (*removed_callback)(size_t, void *),
			    void *arg)
{
	return unicode_bidi_count_or_cleanup(string, string, levels, n,
					     cleanup_options, removed_callback,
					     arg);
}

size_t unicode_bidi_cleaned_size(const char32_t *string,
				 size_t n,
				 int cleanup_options)
{
	return unicode_bidi_count_or_cleanup(string, NULL, NULL, n,
					     cleanup_options, NULL, NULL);
}

void unicode_bidi_logical_order(char32_t *string,
				unicode_bidi_level_t *levels,
				size_t n,
				unicode_bidi_level_t paragraph_embedding,
				void (*reorder_callback)(size_t, size_t,
							 void *),
				void *arg)
{
	size_t i=0;

	// On this pass:
	//
	// When paragraph_embedding is 0, we reverse odd embedding levels.
	// When paragraph_embedding is 1, we reverse even embedding levels.

#define LOGICAL_FLIP(n) ( ((n) ^ paragraph_embedding) & 1)

	while (i<n)
	{
		if ( !LOGICAL_FLIP(levels[i]))
		{
			++i;
			continue;
		}

		size_t j=i;

		while (i<n)
		{
			if (!LOGICAL_FLIP(levels[i]))
				break;
			++i;
		}

		reverse_str(string, levels, j, i,
			    reorder_callback, arg);
	}

	if (paragraph_embedding & 1)
		reverse_str(string, levels, 0, n, reorder_callback, arg);
}

/*
** Track consecutive sequences of characters with the same embedding level.
**
** Linked list create in compute_bidi_embed_levelruns().
*/

struct bidi_embed_levelrun {
	struct bidi_embed_levelrun *next;
	size_t start;
	size_t end;
	unicode_bidi_level_t level;
};

static struct bidi_embed_levelrun **
record_bidi_embed_levelrun(struct bidi_embed_levelrun **tailp,
			   size_t start,
			   size_t end,
			   unicode_bidi_level_t level)
{
	struct bidi_embed_levelrun *p;

	p=(struct bidi_embed_levelrun *)calloc(1, sizeof(*p));
	if (!p)
		abort();

	p->start=start;
	p->end=end;
	p->level=level;

	if (*tailp)
	{
		(*tailp)->next=p;
		return &(*tailp)->next;
	}
	else
	{
		*tailp=p;
		return tailp;
	}
}

static void compute_bidi_embed_levelruns(const char32_t *string,
					 const unicode_bidi_level_t *levels,
					 size_t n,
					 struct bidi_embed_levelrun **tailp)
{
	size_t i=0;

	while (i<n)
	{
		size_t j=i;

		while (++i < n)
		{
			if ((levels[i] & 1) != (levels[j] & 1))
				break;
		}
		tailp=record_bidi_embed_levelrun(tailp, j, i,
						 levels[j] & 1);
	}
}

/*
** Whether a directional marker and a UNICODE_PDI is required to be generated after
** some subset of characters.
*/

struct need_marker_info {
	int need_marker;
	int need_pdi;
};

static void need_marker_info_init(struct need_marker_info *info)
{
	info->need_marker=0;
	info->need_pdi=0;
}

static void need_marker_info_merge(struct need_marker_info *info,
				   const struct need_marker_info *other_info)
{
	if (other_info->need_marker)
		info->need_marker=1;
	if (other_info->need_pdi)
		info->need_pdi=1;
}

static void emit_bidi_embed_levelrun(const char32_t *string,
				     enum_bidi_type_t *types,
				     struct bidi_embed_levelrun *run,
				     unicode_bidi_level_t paragraph_level,
				     unicode_bidi_level_t previous_level,
				     unicode_bidi_level_t next_level,
				     struct need_marker_info *need_marker,
				     void (*emit)(const char32_t *string,
						  size_t n,
						  int is_part_of_string,
						  void *arg),
				     void *arg);

/* L1 */

static int is_l1_on_or_after(const enum_bidi_type_t *types,
			     size_t n,
			     size_t i,
			     int atend)
{
	/*
	** Determine if rule L1 will apply starting at the given position.
	*/
	while (i<n)
	{
		enum_bidi_type_t t=types[i];

		if (t == UNICODE_BIDI_TYPE_WS)
		{
			++i;
			continue;
		}

		if (t == UNICODE_BIDI_TYPE_S ||
		    t == UNICODE_BIDI_TYPE_B)
			return 1;
		return 0;
	}
	return atend;
}

static void emit_marker(struct bidi_embed_levelrun *p,
			struct need_marker_info *info,
			void (*emit)(const char32_t *string,
				     size_t n,
				     int is_part_of_string,
				     void *arg),
			void *arg)
{
	char32_t marker= (p->level & 1) ? UNICODE_RLM:UNICODE_LRM;

	if (info->need_marker)
		(*emit)(&marker, 1, 0, arg);

	if (info->need_pdi)
	{
		marker=UNICODE_PDI;
		(*emit)(&marker, 1, 0, arg);
	}
}

int unicode_bidi_needs_embed(const char32_t *string,
			     const unicode_bidi_level_t *levels,
			     size_t n,
			     const unicode_bidi_level_t *paragraph_level)
{
	char32_t *string_cpy=(char32_t *)malloc(n * sizeof(char32_t));
	unicode_bidi_level_t *levels_cpy=(unicode_bidi_level_t *)
		malloc(n * sizeof(unicode_bidi_level_t));
	size_t nn;
	int ret;

	if (!string_cpy || !levels_cpy)
		abort();

	memcpy(string_cpy, string, n * sizeof(char32_t));

	struct unicode_bidi_direction direction=
		unicode_bidi_calc(string_cpy, n,
				  levels_cpy, paragraph_level);

	unicode_bidi_reorder(string_cpy, levels_cpy, n, NULL, NULL);
	nn=unicode_bidi_cleanup(string_cpy, levels_cpy, n, 0,
				NULL, NULL);

	ret=1;
	if (n == nn && (paragraph_level == NULL ||
			direction.direction == *paragraph_level))
	{
		unicode_bidi_logical_order(string_cpy, levels_cpy, nn,
					   direction.direction,
					   NULL, NULL);
		if (memcmp(string_cpy, string, n * sizeof(char32_t)) == 0 &&
		    memcmp(levels_cpy, levels, n * sizeof(unicode_bidi_level_t))
		    == 0)
		{
			ret=0;
		}
	}
	free(string_cpy);
	free(levels_cpy);
	return ret;
}

void unicode_bidi_embed(const char32_t *string,
			const unicode_bidi_level_t *levels,
			size_t n,
			unicode_bidi_level_t paragraph_level,
			void (*emit)(const char32_t *string,
				     size_t n,
				     int is_part_of_string,
				     void *arg),
			void *arg)
{
	struct bidi_embed_levelrun *runs=0;
	enum_bidi_type_t *types=
		(enum_bidi_type_t *)calloc(n, sizeof(enum_bidi_type_t));

	if (!types)
		abort();

	for (size_t i=0; i<n; ++i)
		types[i]=unicode_bidi_type(string[i]);

	compute_bidi_embed_levelruns(string, levels,
				     n,
				     &runs);

	/*
	** Go through the sequences of consecutive characters with the
	** same embedding level. Keep track of the preceding and the
	** next embedding level, which is usually the opposite from the
	** current sequence's embedding level. Except that the first and
	** the last sequence of characters, in the string, are bound to
	** the paragraph_level, which may be the same.
	*/

	unicode_bidi_level_t previous_level=paragraph_level;

	while (runs)
	{
		struct bidi_embed_levelrun *p=runs;

		runs=runs->next;

		unicode_bidi_level_t next_level=paragraph_level;

		if (runs)
			next_level=runs->level;

#ifdef BIDI_DEBUG
		fprintf(DEBUGDUMP, "  Range %d-%d, level %d\n",
			(int)p->start, (int)(p->end-1), p->level);
#endif

		if (((p->level ^ paragraph_level) & 1) == 0)
		{
			/*
			** Sequence in the same direction as the paragraph
			** embedding level.
			**
			** We'll definitely need a directional marker if
			** rule L1 applies after this sequence.
			*/

			struct need_marker_info need_marker;

			need_marker_info_init(&need_marker);

			if (types[p->end-1] == UNICODE_BIDI_TYPE_WS)
			{
				need_marker.need_marker=
					is_l1_on_or_after(types, n,
							  p->end,
							  0);
#ifdef BIDI_DEBUG
				fprintf(DEBUGDUMP, "    need marker=%d\n",
					need_marker.need_marker);
#endif

			}

			emit_bidi_embed_levelrun(string, types,
						 p, paragraph_level,
						 previous_level,
						 next_level,
						 &need_marker,
						 emit, arg);

			emit_marker(p, &need_marker, emit, arg);
		}
		else
		{
			struct need_marker_info need_marker;
			size_t orig_end=p->end;

			/*
			** Sequence in the opposite direction. Because S and
			** B reset to the paragraph level, no matter what,
			** if we want things to render like that we will need
			** to emit sequences on each side of S/B in reverse
			** order. We start at the end of this sequence, then
			** search towards the beginning, emit that sequence,
			** emit the S and B, then go to the next sequence.
			*/

			need_marker_info_init(&need_marker);

#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP, "    need marker=%d\n",
				need_marker.need_marker);
#endif

			while (p->start < p->end)
			{
				size_t j=p->end;

				int end_with_ws=
					types[j-1] == UNICODE_BIDI_TYPE_WS;
				while (j > p->start)
				{
					--j;

					enum_bidi_type_t t=types[j];

					if (t == UNICODE_BIDI_TYPE_S ||
					    t == UNICODE_BIDI_TYPE_B)
					{
						++j;
						break;
					}
				}

				if (j == p->end) /* Must be lone break */
				{
#ifdef BIDI_DEBUG
					fprintf(DEBUGDUMP,
						"    break: %d\n",
						(int)j);
#endif
					--p->end;

					previous_level=paragraph_level;

					(*emit)(string+p->end, 1, 1, arg);
					continue;
				}

				struct need_marker_info need_marker_partial;

				need_marker_info_init(&need_marker_partial);

				/*
				** Rule L1, there's going to be an S or a B
				** after we emit this sequence.
				*/

				if (j != p->start)
					need_marker_partial.need_marker=1;

				/*
				** To emit this sequence, we monkey-patch
				** the run level to indicate the sub-
				** sequence to emit.
				*/
				size_t i=p->start;

				p->start=j;

				emit_bidi_embed_levelrun
					(string, types, p, paragraph_level,
					 previous_level,

					 j == i
					 /* No more, this is next */
					 ? next_level
					 /* We'll emit a paragraph brk */
					 : paragraph_level,
					 &need_marker_partial,
					 emit, arg);

				/* Continue monkey-patching. */

				p->end=p->start;
				p->start=i;

				if (p->start == p->end)
					/* Do it below */
				{
					if (end_with_ws)
						need_marker.need_marker=
							is_l1_on_or_after
							(types, n,
							 orig_end,
							 0);
					need_marker_info_merge
						(&need_marker,
						 &need_marker_partial);
				}
				else
				{
					emit_marker(p, &need_marker_partial,
						    emit, arg);
				}
			}
			emit_marker(p, &need_marker, emit, arg);
		}
		free(p);
	}
	free(types);
}

#define ADJUST_LR(t,e) do {					\
		switch (t) {					\
		case UNICODE_BIDI_TYPE_AL:			\
			(t)=UNICODE_BIDI_TYPE_R;		\
			break;					\
		case UNICODE_BIDI_TYPE_ET:			\
		case UNICODE_BIDI_TYPE_ES:			\
		case UNICODE_BIDI_TYPE_AN:			\
		case UNICODE_BIDI_TYPE_EN:			\
			(t)=UNICODE_BIDI_TYPE_L;		\
			break;					\
		default:					\
			break;					\
		}						\
	} while (0)

#define ADJUST_LRSTRONG(t) do {					\
		switch (t) {					\
		case UNICODE_BIDI_TYPE_AL:			\
			(t)=UNICODE_BIDI_TYPE_R;		\
		default:					\
			break;					\
		}						\
	} while (0)

static void emit_bidi_embed_levelrun(const char32_t *string,
				     enum_bidi_type_t *types,
				     struct bidi_embed_levelrun *run,
				     unicode_bidi_level_t paragraph_level,
				     unicode_bidi_level_t previous_level,
				     unicode_bidi_level_t next_level,
				     struct need_marker_info *need_marker,
				     void (*emit)(const char32_t *string,
						  size_t n,
						  int is_part_of_string,
						  void *arg),
				     void *arg)
{
	/*
	** Our first order of business will be to apply rules W to this
	** sequence, to resolve weak types.
	**
	** It's easy to simulate what unicode_bidi_w() expects.
	*/

	struct level_run lrun;
	struct isolating_run_sequence_s seq;
	enum_bidi_type_t e_type=E_CLASS(run->level);
	enum_bidi_type_t o_type=O_CLASS(run->level);

	if (run->start == run->end)
		return;

	memset(&seq, 0, sizeof(seq));

	seq.embedding_level=run->level;
	seq.sos=seq.eos=e_type;
	seq.runs.runs=&lrun;
	seq.runs.n_level_runs=1;
	seq.runs.cap_level_runs=1;
	lrun.start=run->start;
	lrun.end=run->end;
	unicode_bidi_w(types, &seq);

	/*
	** Peek at the first character's class.
	**
	** If the previous sequence's embedding level was the same, it
	** guarantees the peristence of the embedding direction. We can
	** accept types that default to our embedding level.
	**
	** Otherwise we recognize only strong types.
	*/
	enum_bidi_type_t t=types[run->start];

	if (previous_level == run->level)
	{
		ADJUST_LR(t, E_CLASS(previous_level));
	}
	else
	{
		ADJUST_LRSTRONG(t);
	}

	/*
	** Sequence in the opposite direction always get isolated.
	*/
	char32_t override_start=run->level ? UNICODE_RLI:UNICODE_LRI;

	if (run->level != paragraph_level)
		(*emit)(&override_start, 1, 0, arg);

	/*
	** Make sure the character sequence has strong context.
	*/
	if (t == o_type)
	{
		struct need_marker_info need_marker;

		need_marker_info_init(&need_marker);

		need_marker.need_marker=1;

		emit_marker(run, &need_marker, emit, arg);
	}

	override_start=run->level ? UNICODE_RLO:UNICODE_LRO;
	char32_t override_end=UNICODE_PDF;

	size_t start=run->start;
	size_t end=run->end;

	while (start < end)
	{
		size_t i=start;
		size_t word_start=i;

#ifdef BIDI_DEBUG
		fprintf(DEBUGDUMP,
			"    examining, starting at: %d\n", (int)i);
#endif

		/*
		** Look for the next character with the opposite class.
		** While doing that, keep an eye out on any WS or ONs,
		** which will tell us where the most recent "word"s starts,
		** before this character.
		*/
		while (i < end)
		{
			enum_bidi_type_t t=types[i];

			ADJUST_LR(t, e_type);

			if (t == o_type)
				break;

			switch (t) {
			case UNICODE_BIDI_TYPE_WS:
			case UNICODE_BIDI_TYPE_ON:
				word_start=i+1;
				break;
			default:
				break;
			}

			++i;
		}

		if (i < end)
		{
#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP,
				"    override needed: %d,"
				" start of word at %d, ",
				(int)i, (int)word_start);
#endif
			/*
			** Found something to override. First, emit everything
			** up to the start of this "word".
			**
			** Then emit the RLO or LRO, then look for the end
			** of the "word", and drop the UNICODE_PDF there.
			*/
			if (word_start > start)
				(*emit)(string+start,
					word_start-start, 1, arg);

			(*emit)(&override_start, 1, 0, arg);
			while (++i < end)
			{
				enum_bidi_type_t t=types[i];

				switch (t) {
				case UNICODE_BIDI_TYPE_WS:
				case UNICODE_BIDI_TYPE_ON:
					break;
				default:
					continue;
				}
				break;
			}
#ifdef BIDI_DEBUG
			fprintf(DEBUGDUMP, "end of word at %d\n",
				(int)i);
#endif
			(*emit)(string+word_start, i-word_start, 1, arg);
			(*emit)(&override_end, 1, 0, arg);
			start=i;
			continue;
		}
		(*emit)(string+start, i-start, 1, arg);
		start=i;
	}

	/*
	** Make sure that if a different embedding level follows we will
	** emit a marker, to ensure strong context.
	*/
	t=types[run->end-1];

	if (next_level != run->level)
	{
		ADJUST_LRSTRONG(t);

		if (e_type != t)
			need_marker->need_marker=1;
	}

	if (run->level != paragraph_level)
		need_marker->need_pdi=1;
}

struct compute_paragraph_embedding_level_char_info {
	const char32_t *str;
};

static enum_bidi_type_t
get_enum_bidi_type_for_embedding_paragraph_level(size_t i,
						 void *arg)
{
	struct compute_paragraph_embedding_level_char_info *p=
		(struct compute_paragraph_embedding_level_char_info *)arg;

	return unicode_bidi_type(p->str[i]);
}

char32_t unicode_bidi_embed_paragraph_level(const char32_t *str,
					    size_t n,
					    unicode_bidi_level_t paragraph_level
					    )
{
	struct compute_paragraph_embedding_level_char_info info;
	info.str=str;

	if ((compute_paragraph_embedding_level
	     (0, n,
	      get_enum_bidi_type_for_embedding_paragraph_level,
	      &info).direction ^ paragraph_level) == 0)
		return 0;

	return (paragraph_level & 1) ? UNICODE_RLM:UNICODE_LRM;
}

struct unicode_bidi_direction unicode_bidi_get_direction(const char32_t *str,
							 size_t n)
{
	struct compute_paragraph_embedding_level_char_info info;

	info.str=str;
	return compute_paragraph_embedding_level
		(0, n,
		 get_enum_bidi_type_for_embedding_paragraph_level, &info);
}

void unicode_bidi_combinings(const char32_t *str,
			     const unicode_bidi_level_t *levels,
			     size_t n,
			     void (*combinings)(unicode_bidi_level_t level,
						size_t level_start,
						size_t n_chars,
						size_t comb_start,
						size_t n_comb_chars,
						void *arg),
			       void *arg)
{
	size_t level_start=0;

	while (level_start < n)
	{
		size_t level_end;
		size_t comb_start;
		size_t comb_end;

		// Find the end of this level

		for (level_end=level_start; ++level_end<n; )
		{
			if (levels && (levels[level_end] !=
				       levels[level_start]))
				break;
		}

		// Now sweep from level_start to level_end.

		for (comb_start=level_start; comb_start < level_end; )
		{
			// Search for a non-0 ccc

			if (unicode_ccc(str[comb_start]) == 0)
			{
				++comb_start;
				continue;
			}

			// Now, search for the next ccc of 0, stopping at
			// level_end

			for (comb_end=comb_start; ++comb_end < level_end; )
			{
				if (unicode_ccc(str[comb_end]) == 0)
					break;
			}

			// Report this
			(*combinings)((levels ? levels[level_start]
					 : 0), level_start,
				      level_end-level_start,
				      comb_start,
				      comb_end-comb_start, arg);

			// If we're here before the level_end we must
			// have reached the next starter. So, on the next
			// iteration we want to start with the following
			// character. So, if the callback reversed the
			// combinings and the following starter the
			// next character will now be a composition, so
			// we can skip it.

			if (comb_end < level_end)
				++comb_end;
			comb_start=comb_end;
		}
		level_start=level_end;
	}
}
