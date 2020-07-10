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

/* BIDI_CLASS_LIST */
typedef enum {
	      UNICODE_BIDI_CLASS_AL,
	      UNICODE_BIDI_CLASS_AN,
	      UNICODE_BIDI_CLASS_B,
	      UNICODE_BIDI_CLASS_BN,
	      UNICODE_BIDI_CLASS_CS,
	      UNICODE_BIDI_CLASS_EN,
	      UNICODE_BIDI_CLASS_ES,
	      UNICODE_BIDI_CLASS_ET,
	      UNICODE_BIDI_CLASS_FSI,
	      UNICODE_BIDI_CLASS_L,
	      UNICODE_BIDI_CLASS_LRE,
	      UNICODE_BIDI_CLASS_LRI,
	      UNICODE_BIDI_CLASS_LRO,
	      UNICODE_BIDI_CLASS_NSM,
	      UNICODE_BIDI_CLASS_ON,
	      UNICODE_BIDI_CLASS_PDF,
	      UNICODE_BIDI_CLASS_PDI,
	      UNICODE_BIDI_CLASS_R,
	      UNICODE_BIDI_CLASS_RLE,
	      UNICODE_BIDI_CLASS_RLI,
	      UNICODE_BIDI_CLASS_RLO,
	      UNICODE_BIDI_CLASS_S,
	      UNICODE_BIDI_CLASS_WS,
} enum_bidi_class_t;

#include "bidi_class.h"

#ifdef BIDI_DEBUG
enum_bidi_class_t fudge_unicode_bidi(size_t);
const char *bidi_classname(enum_bidi_class_t);
#endif

#define max_depth 125

typedef enum {
	      do_neutral,
	      do_right_to_left,
	      do_left_to_right
} directional_override_status_t;

#define is_isolate_initiator(c)				\
	((c) == UNICODE_BIDI_CLASS_LRI ||		\
	 (c) == UNICODE_BIDI_CLASS_RLI ||		\
	 (c) == UNICODE_BIDI_CLASS_FSI)

#define is_embedding_initiator(c)			\
	((c) == UNICODE_BIDI_CLASS_LRE ||		\
	 (c) == UNICODE_BIDI_CLASS_RLE ||		\
	 (c) == UNICODE_BIDI_CLASS_LRO ||		\
	 (c) == UNICODE_BIDI_CLASS_RLO)

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
	enum_bidi_class_t sos, eos;
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

	unicode_bidi_level_t paragraph_embedding_level;
	const char32_t    *chars;
	enum_bidi_class_t *classes;
	enum_bidi_class_t *orig_classes;
	unicode_bidi_level_t *levels;
	size_t size;
	int overflow_isolate_count;
	int overflow_embedding_count;
	int valid_isolate_count;

	struct isolating_run_sequences_s isolating_run_sequences;
} *directional_status_stack_t;

#ifdef BIDI_DEBUG
void dump_classes(const char *prefix, directional_status_stack_t stack)
{
	fprintf(DEBUGDUMP, "%s: ", prefix);
	for (size_t i=0; i<stack->size; ++i)
	{
		fprintf(DEBUGDUMP, " %s(%d)",
			bidi_classname(stack->classes[i]),
			(int)stack->levels[i]);
	}
	fprintf(DEBUGDUMP, "\n");
}

void dump_orig_classes(const char *prefix, directional_status_stack_t stack)
{
	fprintf(DEBUGDUMP, "%s: ", prefix);

	for (size_t i=0; i<stack->size; ++i)
	{
		fprintf(DEBUGDUMP, " %s(%s%s%d)",
			bidi_classname(stack->classes[i]),
			(stack->classes[i] != stack->orig_classes[i] ?
			 bidi_classname(stack->orig_classes[i]):""),
			(stack->classes[i] != stack->orig_classes[i] ? "/":""),
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

static unicode_bidi_level_t
compute_paragraph_embedding_level(const enum_bidi_class_t *p,
				  size_t i, size_t j)
{
	unicode_bidi_level_t in_isolation=0;

	for (; i<j; ++i)
	{
		if (is_isolate_initiator(p[i]))
			++in_isolation;
		else if (p[i] == UNICODE_BIDI_CLASS_PDI)
		{
			if (in_isolation)
				--in_isolation;
		}

		if (in_isolation == 0)
		{
			if (p[i] == UNICODE_BIDI_CLASS_AL ||
			    p[i] == UNICODE_BIDI_CLASS_R)
			{
				return 1;
			}
			if (p[i] == UNICODE_BIDI_CLASS_L)
				break;
		}
	}
	return 0;
}

static directional_status_stack_t
directional_status_stack_init(const char32_t *chars,
			      enum_bidi_class_t *classes, size_t n,
			      unicode_bidi_level_t *levels,
			      const unicode_bidi_level_t
			      *initial_embedding_level)
{
	directional_status_stack_t stack;

	stack=(directional_status_stack_t)calloc(1, sizeof(*stack));

	stack->paragraph_embedding_level=
		initial_embedding_level
		? *initial_embedding_level & 1
		: compute_paragraph_embedding_level(classes, 0, n);
	stack->chars=chars;
	stack->classes=classes;

	if (n)
	{
		classes=(enum_bidi_class_t *)
			malloc(sizeof(enum_bidi_class_t)*n);
		if (!classes)
			abort();
		memcpy(classes, stack->classes, sizeof(enum_bidi_class_t)*n);
	}
	else
	{
		classes=0;
	}
	stack->orig_classes=classes;
	stack->levels=levels;
	stack->size=n;

	directional_status_stack_push(stack,
				      stack->paragraph_embedding_level,
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
	if (stack->orig_classes)
		free(stack->orig_classes);
	isolating_run_sequences_deinit(&stack->isolating_run_sequences);
	free(stack);
}

static void unicode_bidi_b(const char32_t *p,
			   size_t n,
			   enum_bidi_class_t *buf,
			   unicode_bidi_level_t *bufp,
			   const unicode_bidi_level_t *initial_embedding_level);

void unicode_bidi_calc(const char32_t *p, size_t n, unicode_bidi_level_t *bufp,
		       const unicode_bidi_level_t *initial_embedding_level)
{
	/*
	** Look up the bidi class for each char32_t.
	**
	** When we encounter a paragraph break we call unicode_bidi_b() to
	** process it.
	*/

	enum_bidi_class_t *buf=
		(enum_bidi_class_t *)malloc(n * sizeof(enum_bidi_class_t));

	for (size_t i=0; i<n; ++i)
	{
		buf[i]=(enum_bidi_class_t)
			unicode_tab_lookup(p[i],
					   unicode_indextab,
					   sizeof(unicode_indextab)
					   /sizeof(unicode_indextab[0]),
					   unicode_rangetab,
					   unicode_classtab,
					   UNICODE_BIDI_CLASS_L);
#ifdef UNICODE_BIDI_TEST
		UNICODE_BIDI_TEST(i);
#endif
		bufp[i]=UNICODE_BIDI_SKIP;
	}

	unicode_bidi_b(p, n,
		       buf,
		       bufp,
		       initial_embedding_level);

	free(buf);
}

static void unicode_bidi_cl(directional_status_stack_t stack);

static void unicode_bidi_b(const char32_t *p,
			   size_t n,
			   enum_bidi_class_t *buf,
			   unicode_bidi_level_t *bufp,
			   const unicode_bidi_level_t *initial_embedding_level)
{
	directional_status_stack_t stack;

	stack=directional_status_stack_init(p, buf, n, bufp,
					    initial_embedding_level);

#ifdef BIDI_DEBUG
	fprintf(DEBUGDUMP, "BIDI: START: Paragraph embedding level: %d\n",
		(int)stack->paragraph_embedding_level);
#endif

	unicode_bidi_cl(stack);

	directional_status_stack_deinit(stack);
}

#define RESET_CLASS(p,stack) do {				\
		switch ((stack)->head->directional_override_status) {	\
		case do_neutral: break;					\
		case do_left_to_right: (p)=UNICODE_BIDI_CLASS_L; break; \
		case do_right_to_left: (p)=UNICODE_BIDI_CLASS_R; break; \
		}							\
	} while(0)

static void unicode_bidi_w(directional_status_stack_t stack,
			   struct isolating_run_sequence_s *seq);
static void unicode_bidi_n(directional_status_stack_t stack,
			   struct isolating_run_sequence_s *seq);

#ifdef BIDI_DEBUG
void dump_sequence_info(directional_status_stack_t stack,
			struct isolating_run_sequence_s *seq)
{
	fprintf(DEBUGDUMP, "Sequence: sos: %c, eos: %c:",
		(seq->sos == UNICODE_BIDI_CLASS_L ? 'L':'R'),
		(seq->eos == UNICODE_BIDI_CLASS_L ? 'L':'R'));

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
			bidi_classname(stack->classes[beg.i]),
			(int)stack->levels[beg.i]);
		irs_incr(&beg);
	}
	fprintf(DEBUGDUMP, "\n");
}
#endif

static void unicode_bidi_cl(directional_status_stack_t stack)
{
#ifdef BIDI_DEBUG
	dump_classes("Before X1", stack);
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

		switch (stack->classes[i]) {
		case UNICODE_BIDI_CLASS_RLE:
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
		case UNICODE_BIDI_CLASS_LRE:
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

		case UNICODE_BIDI_CLASS_RLO:
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

		case UNICODE_BIDI_CLASS_LRO:
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

		enum_bidi_class_t cur_class=stack->classes[i];

		if (cur_class == UNICODE_BIDI_CLASS_FSI) {
			/* X5c */

			size_t j=i;

			unicode_bidi_level_t in_isolation=1;

			while (++j < stack->size)
			{
				if (is_isolate_initiator(stack->classes[j]))
					++in_isolation;
				else if (stack->classes[j] == UNICODE_BIDI_CLASS_PDI)
				{
					if (--in_isolation == 0)
						break;
				}
			}

			cur_class=compute_paragraph_embedding_level
				(stack->classes, i+1, j) == 1
				? UNICODE_BIDI_CLASS_RLI
				: UNICODE_BIDI_CLASS_LRI;
		}

		switch (cur_class) {
		case UNICODE_BIDI_CLASS_RLI:
			/* X5a */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->classes[i],stack);

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

		case UNICODE_BIDI_CLASS_LRI:
			/* X5b */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->classes[i],stack);

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

		switch (stack->orig_classes[i]) {
		case UNICODE_BIDI_CLASS_BN:
		case UNICODE_BIDI_CLASS_B:
		case UNICODE_BIDI_CLASS_RLE:
		case UNICODE_BIDI_CLASS_LRE:
		case UNICODE_BIDI_CLASS_RLO:
		case UNICODE_BIDI_CLASS_LRO:
		case UNICODE_BIDI_CLASS_PDF:
		case UNICODE_BIDI_CLASS_RLI:
		case UNICODE_BIDI_CLASS_LRI:
		case UNICODE_BIDI_CLASS_FSI:
		case UNICODE_BIDI_CLASS_PDI:
			break;
		default:
			/* X6 */
			stack->levels[i]=stack->head->embedding_level;
			RESET_CLASS(stack->classes[i],stack);
			break;
		}

		if (stack->classes[i] == UNICODE_BIDI_CLASS_PDI)
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
			RESET_CLASS(stack->classes[i],stack);
		}

		if (stack->classes[i] == UNICODE_BIDI_CLASS_PDF)
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

		if (stack->classes[i] == UNICODE_BIDI_CLASS_B)
		{
			/* X8 */

			stack->levels[i]=stack->paragraph_embedding_level;
		}
	}

	/* X9 */

#define IS_X9(class)						\
		((class) == UNICODE_BIDI_CLASS_RLE ||		\
		 (class) == UNICODE_BIDI_CLASS_LRE ||		\
		 (class) == UNICODE_BIDI_CLASS_RLO ||		\
		 (class) == UNICODE_BIDI_CLASS_LRO ||		\
		 (class) == UNICODE_BIDI_CLASS_PDF ||		\
		 (class) == UNICODE_BIDI_CLASS_BN)

	size_t next_pdi=0;
	struct isolating_run_sequence_s *current_irs=0;
	unicode_bidi_level_t current_level=0;

	struct isolating_run_sequence_link **push_links=
		sort_irs_links_by_push(&stack->isolating_run_sequences);
	size_t next_rlilri=0;

	for (size_t i=0; i<stack->size; ++i)
	{
		if (IS_X9(stack->classes[i]))
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
	dump_classes("Before X10", stack);
#endif

	for (struct isolating_run_sequence_s *p=
		     stack->isolating_run_sequences.head; p; p=p->next)
	{
		p->sos=p->eos=UNICODE_BIDI_CLASS_L;

		irs_iterator beg_iter=irs_begin(p), end_iter=irs_end(p);

		if (irs_compare(&beg_iter, &end_iter) == 0)
			continue; /* Edge case */

		unicode_bidi_level_t before=
			stack->paragraph_embedding_level;
		unicode_bidi_level_t after=
			stack->paragraph_embedding_level;


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

		if (!is_isolate_initiator(stack->classes[end_iter.i]))
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
			p->sos=UNICODE_BIDI_CLASS_R;
		if (after & 1)
			p->eos=UNICODE_BIDI_CLASS_R;


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
		dump_sequence("Contents before W", stack, p);
#endif

		unicode_bidi_w(stack, p);

#ifdef BIDI_DEBUG
		dump_sequence("Contents after W", stack, p);
#endif
		unicode_bidi_n(stack, p);
	}
#ifdef BIDI_DEBUG
	dump_orig_classes("Before L1", stack);
#endif

	/*
	** L1
	*/

	size_t i=stack->size;

	int seen_sb=1;

	while (i > 0)
	{
		--i;

		if (IS_X9(stack->orig_classes[i]))
			continue;

		switch (stack->orig_classes[i]) {
		case UNICODE_BIDI_CLASS_WS:
		case UNICODE_BIDI_CLASS_FSI:
		case UNICODE_BIDI_CLASS_LRI:
		case UNICODE_BIDI_CLASS_RLI:
		case UNICODE_BIDI_CLASS_PDI:
			if (seen_sb)
				stack->levels[i]=
					stack->paragraph_embedding_level;
			break;
		case UNICODE_BIDI_CLASS_S:
		case UNICODE_BIDI_CLASS_B:
			stack->levels[i]=stack->paragraph_embedding_level;
			seen_sb=1;
			break;
		default:
			seen_sb=0;
			continue;
		}
	}
}

static void unicode_bidi_w(directional_status_stack_t stack,
			   struct isolating_run_sequence_s *seq)
{
	irs_iterator iter=irs_begin(seq), end=irs_end(seq);
	enum_bidi_class_t previous_type=seq->sos;

	enum_bidi_class_t strong_type=UNICODE_BIDI_CLASS_R;

	while (irs_compare(&iter, &end))
	{
		if (stack->classes[iter.i] == UNICODE_BIDI_CLASS_NSM)
		{
			/* W1 */
			stack->classes[iter.i] =
				is_isolate_initiator(previous_type) ||
				previous_type == UNICODE_BIDI_CLASS_PDI
				? UNICODE_BIDI_CLASS_ON
				: previous_type;

		}

		/* W2 */

		if (stack->classes[iter.i] == UNICODE_BIDI_CLASS_EN &&
		    strong_type == UNICODE_BIDI_CLASS_AL)
		{
			stack->classes[iter.i] = UNICODE_BIDI_CLASS_AN;
		}

		/* W2 */
		previous_type=stack->classes[iter.i];

		switch (previous_type) {
		case UNICODE_BIDI_CLASS_R:
		case UNICODE_BIDI_CLASS_L:
		case UNICODE_BIDI_CLASS_AL:
			strong_type=previous_type;
			break;
		default:
			break;
		}

		irs_incr(&iter);
	}

	iter=irs_begin(seq);

	previous_type=UNICODE_BIDI_CLASS_L;

	int not_eol=irs_compare(&iter, &end);

	while (not_eol)
	{
		/* W3 */
		if (stack->classes[iter.i] == UNICODE_BIDI_CLASS_AL)
			stack->classes[iter.i] = UNICODE_BIDI_CLASS_R;

		/* W4 */

		enum_bidi_class_t this_type=stack->classes[iter.i];
		irs_incr(&iter);

		not_eol=irs_compare(&iter, &end);

		if (not_eol &&
		    (
		     (this_type == UNICODE_BIDI_CLASS_ES &&
		      previous_type == UNICODE_BIDI_CLASS_EN)
		     ||
		     (this_type == UNICODE_BIDI_CLASS_CS &&
		      (previous_type == UNICODE_BIDI_CLASS_EN ||
		       previous_type == UNICODE_BIDI_CLASS_AN)
		      )
		     ) &&
		    stack->classes[iter.i] == previous_type)
		{
			irs_iterator prev=iter;

			irs_decr(&prev);

			stack->classes[prev.i]=previous_type;
		}

		if (not_eol)
			previous_type=this_type;
	}

	iter=irs_begin(seq);

	/* W5 */

	previous_type=UNICODE_BIDI_CLASS_L; /* Doesn't match any part of W5 */

	while (irs_compare(&iter, &end))
	{
		if (stack->classes[iter.i] != UNICODE_BIDI_CLASS_ET)
		{
			previous_type=stack->classes[iter.i];
			irs_incr(&iter);
			continue;
		}

		/* ET after EN */
		if (previous_type == UNICODE_BIDI_CLASS_EN)
		{
			stack->classes[iter.i] = UNICODE_BIDI_CLASS_EN;
			irs_incr(&iter);
			continue;
		}

		/* See if EN follows these ETs */

		irs_iterator start=iter;

		while (irs_incr(&iter), irs_compare(&iter, &end))
		{
			previous_type=stack->classes[iter.i];

			if (previous_type == UNICODE_BIDI_CLASS_ET)
				continue;

			if (previous_type == UNICODE_BIDI_CLASS_EN)
			{
				while (irs_compare(&start, &iter))
				{
					stack->classes[start.i]=
						UNICODE_BIDI_CLASS_EN;
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
		switch (stack->classes[iter.i]) {
		case UNICODE_BIDI_CLASS_ET:
		case UNICODE_BIDI_CLASS_ES:
		case UNICODE_BIDI_CLASS_CS:
			/* W6 */
			stack->classes[iter.i]=UNICODE_BIDI_CLASS_ON;
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
		switch (stack->classes[iter.i]) {
		case UNICODE_BIDI_CLASS_L:
		case UNICODE_BIDI_CLASS_R:
			previous_type=stack->classes[iter.i];
			break;
		case UNICODE_BIDI_CLASS_EN:
			if (previous_type == UNICODE_BIDI_CLASS_L)
				stack->classes[iter.i]=previous_type;
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
		unicode_bidi_bracket_type_t bracket_type;
		char32_t open_bracket=
			unicode_bidi_bracket_type(stack->chars[iter.i],
						  &bracket_type);

		if (bracket_type == UNICODE_BIDI_o)
		{
			if (stackp >= NSTACKSIZE)
				break; /* BD16 failure */

			if (!((*bracket_stack_tail)=(struct bidi_n_stack *)
			      calloc(1, sizeof(struct bidi_n_stack))))
				abort();

			stack_iters[stackp]=*bracket_stack_tail;

			(*bracket_stack_tail)->start=iter;

			stack_chars[stackp]=stack->chars[iter.i];

			bracket_stack_tail= &(*bracket_stack_tail)->next;
			++stackp;
			continue;
		}

		if (bracket_type == UNICODE_BIDI_c) /* Should be "n" */
		{
			for (size_t i=stackp; i > 0; )
			{
				--i;
				if (stack_chars[i] != open_bracket)
					continue;

				stack_iters[i]->end = iter;
				stack_iters[i]->matched=1;
				stackp=i;
				break;
			}
			continue;
		}

		/*
		** So we check if this is an e or an o, and if so we
		** have a convenient list of all unclosed brackets, so
		** we record these facts there.
		*/

		enum_bidi_class_t eoclass=stack->classes[iter.i];

#define ADJUST_EOCLASS(eoclass) do {					\
									\
			if ((eoclass) == UNICODE_BIDI_CLASS_EN ||	\
			    (eoclass) == UNICODE_BIDI_CLASS_AN)		\
				(eoclass)=UNICODE_BIDI_CLASS_R;		\
		} while (0)

		ADJUST_EOCLASS(eoclass);

#define E_CLASS (seq->embedding_level & 1 ?			\
		 UNICODE_BIDI_CLASS_R:UNICODE_BIDI_CLASS_L)

#define O_CLASS (seq->embedding_level & 1 ?			\
		 UNICODE_BIDI_CLASS_L:UNICODE_BIDI_CLASS_R)

		if (eoclass == E_CLASS)
		{
			for (size_t i=0; i<stackp; ++i)
				stack_iters[i]->has_e=1;
		}
		else if (eoclass == O_CLASS)
		{
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

			if (p->has_e)
			{
				stack->classes[p->start.i]=
					stack->classes[p->end.i]=
					seq->embedding_level & 1
					? UNICODE_BIDI_CLASS_R
					: UNICODE_BIDI_CLASS_L;
				set=1;
			} else if (p->has_o)
			{
				enum_bidi_class_t strong_type=seq->sos;

				iter=p->start;

				while (irs_compare(&beg, &iter))
				{
					irs_decr(&iter);

					enum_bidi_class_t eoclass=
						stack->classes[iter.i];

					ADJUST_EOCLASS(eoclass);

					switch (eoclass) {
					case UNICODE_BIDI_CLASS_L:
					case UNICODE_BIDI_CLASS_R:
						break;
					default:
						continue;
					}

					strong_type=eoclass;
					break;
				}

				if (strong_type == O_CLASS)
				{
					stack->classes[p->start.i]=
						stack->classes[p->end.i]=
						strong_type;
					set=1;
				}
			}

			if (set)
			{
				enum_bidi_class_t strong_type=
					stack->classes[p->end.i];

				while (irs_incr(&p->end),
				       irs_compare(&p->end, &end))
				{
					if (stack->orig_classes[p->end.i] !=
					    UNICODE_BIDI_CLASS_NSM)
						break;

					stack->classes[p->end.i]=strong_type;
				}
			}
		}
		free(p);
	}

	/* N1 */

#define IS_NI(class)						\
	((class) == UNICODE_BIDI_CLASS_B ||			\
	 (class) == UNICODE_BIDI_CLASS_S ||			\
	 (class) == UNICODE_BIDI_CLASS_WS ||			\
	 (class) == UNICODE_BIDI_CLASS_ON ||			\
	 (class) == UNICODE_BIDI_CLASS_FSI ||			\
	 (class) == UNICODE_BIDI_CLASS_LRI ||			\
	 (class) == UNICODE_BIDI_CLASS_RLI ||			\
	 (class) == UNICODE_BIDI_CLASS_PDI)

	enum_bidi_class_t prev_type=seq->sos;

	for (iter=beg; irs_compare(&iter, &end); )
	{
		/*
		** N1
		*/

		enum_bidi_class_t this_type=stack->classes[iter.i];

		ADJUST_EOCLASS(this_type);

		if (!IS_NI(this_type))
		{
			switch (this_type) {
			case UNICODE_BIDI_CLASS_L:
			case UNICODE_BIDI_CLASS_R:
				prev_type=this_type;
				break;
			default:
				prev_type=UNICODE_BIDI_CLASS_ON; // Marker.
				break;
			}
			irs_incr(&iter);
			continue;
		}

		enum_bidi_class_t next_type=seq->eos;

		irs_iterator start=iter;

		while (irs_compare(&iter, &end))
		{
			if (IS_NI(stack->classes[iter.i]))
			{
				irs_incr(&iter);
				continue;
			}

			enum_bidi_class_t other_type=stack->classes[iter.i];

			ADJUST_EOCLASS(other_type);

			switch (other_type) {
			case UNICODE_BIDI_CLASS_L:
			case UNICODE_BIDI_CLASS_R:
				next_type=other_type;
				break;
			default:
				next_type=UNICODE_BIDI_CLASS_BN; /* Marker */
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
				stack->classes[start.i]=next_type; /* N1 */
			}

			irs_incr(&start);
		}
	}

	for (iter=beg; irs_compare(&iter, &end); )
	{
		if (IS_NI(stack->classes[iter.i]))
		{
			stack->classes[iter.i]=
				stack->levels[iter.i] & 1 ?
				UNICODE_BIDI_CLASS_R :
				UNICODE_BIDI_CLASS_L; /* N2 */
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
			switch (stack->classes[iter.i]) {
			case UNICODE_BIDI_CLASS_R:
				++stack->levels[iter.i];
				break;
			case UNICODE_BIDI_CLASS_AN:
			case UNICODE_BIDI_CLASS_EN:
				stack->levels[iter.i] += 2;
				break;
			default: break;
			}
		}
		else
		{
			switch (stack->classes[iter.i]) {
			case UNICODE_BIDI_CLASS_L:
			case UNICODE_BIDI_CLASS_AN:
			case UNICODE_BIDI_CLASS_EN:
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

void unicode_bidi_reorder(char32_t *p,
			  unicode_bidi_level_t *levels,
			  size_t n,
			  void (*reorder_callback)(size_t, size_t, void *),
			  void *arg)
{
	/* L2 */

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

	for (size_t i=layers.n_lruns; i; )
	{
		struct level_runs *runs=layers.lruns+ --i;

		for (size_t j=0; j<runs->n_level_runs; ++j)
		{
			size_t start=runs->runs[j].start;
			size_t end=runs->runs[j].end;
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
	}

	level_run_layers_deinit(&layers);
}
