#ifndef pcp_h
#define pcp_h

/*
** Copyright 2001-2002 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <time.h>

struct PCP_new_eventid {
	char *eventid;
} ;

struct PCP_event_participant {
	const char *address;
	const char *eventid;
} ;

struct PCP_save_event {

	const struct PCP_event_participant *event_participants;
	unsigned n_event_participants;

	int write_event_fd;    /* ... read from this file descriptor, or ... */
	char *write_event_buf; /* ... read from this 0-terminated buffer, */

	int flags;		/* flags - see below in PCP_commit */

	int (*write_event_func)(char *, int, void *);
	void *write_event_func_misc_ptr;
} ;

/*
** Structure passed to pcp_commit() and pcp_book().
*/

struct PCP_commit {
	int flags;
#define PCP_OK_CONFLICT	1	/* Ok if time conflict with existing events */
#define PCP_OK_PROXY_ERRORS 2	/*
				** Force through the commit even if there is
				** an error from the proxy server.
				*/

#define PCP_BYPROXY 4       	/*
				** This event is placed on the calendar by
				** proxy (basically, this event has the
				** LIST_PROXY bit set by pcp_retr.
				*/

	const struct PCP_event_time *event_times;
	unsigned n_event_times;

	int errcode;		/* Extended error code */
/*
** PCP error codes:
*/
#define PCP_ERR_SYSERR          0	/* See errno */
#define PCP_ERR_LOCK		1
#define PCP_ERR_CONFLICT	2
#define PCP_ERR_EVENTNOTFOUND	3
#define PCP_ERR_EVENTLOCKED	4

	int (*add_conflict_callback)(const char *, time_t, time_t,
				     const char *, void *);
	void *add_conflict_callback_ptr;

	/*
	** Callback function invoked to specify participant userids whose
	** calendars were updated.
	*/

	void (*proxy_callback)(const char *,	/* "NEW" or "DELETE" */
			       const char *,	/* userid */
			       void *);		/* callback ptr */
	void *proxy_callback_ptr;
} ;

struct PCP_delete {
	const char *id;
	int errcode;
	void (*proxy_callback)(const char *,	/* "NEW" or "DELETE" */
			       const char *,	/* userid */
			       void *);		/* callback ptr */
	void *proxy_callback_ptr;
} ;

struct PCP_event_time {
	time_t start;
	time_t end;
} ;

struct PCP_uncancel {
	int errcode;
	int (*uncancel_conflict_callback)(const char *, time_t, time_t,
					  const char *, void *);
	void *uncancel_conflict_callback_ptr;
} ;

/*
** Structure used by pcp_list_all().  List all events within the specified
** time range.
*/


struct PCP_list_all {

	time_t list_from, list_to;	/* 0, or time_t */

	int (*callback_func)(struct PCP_list_all *, void *);
	void *callback_arg;

	/* The following fields will be initialized */

	time_t event_from, event_to;
	const char *event_id;

#define LIST_CANCELLED	1
#define LIST_BOOKED     2
#define	LIST_PROXY	4

} ;

struct PCP_retr {

	int (*callback_func)(struct PCP_retr *, void *);
	void *callback_arg;

	const char * const * event_id_list;
	/* List of event-ids to retrieve */

	const char *event_id;	/* Event-id being retrieved */
	int errcode;

	int (*callback_retr_status)(struct PCP_retr *, int, void *);
	/* Initialize to retrieve the status of the events */

	int (*callback_retr_date)(struct PCP_retr *, time_t, time_t, void *);
	/* Initialize to retrieve starting/ending times */

	int (*callback_retr_participants)(struct PCP_retr *, const char *,
					  const char *, void *);
	/* Initialize to retrieve other participants */


	/*
	** If callback_headers_func is set callback_begin_func will be
	** followed by 0 or more callback_headers_func(), followed by
	** callback_end_func().
	**
	** If callback_rfc822_func is set, callback_begin_func will be
	** followed by 0 or more callback_rfc822_func(), followed by
	** callback_end_func().
	*/

	int (*callback_begin_func)(struct PCP_retr *, void *);
	int (*callback_headers_func)(struct PCP_retr *,
				     const char *, const char *, void *);
	int (*callback_rfc822_func)(struct PCP_retr *, const char *, int,
				    void *);
	int (*callback_end_func)(struct PCP_retr *, void *);
} ;

/* Calendar metastructure returned by driver-specific open function */

struct PCP {
	void (*close_func)(struct PCP *);
	const char *(*authtoken_func)(struct PCP *);
	int (*cleanup_func)(struct PCP *);

	void (*noop_func)(struct PCP *);
	struct PCP_new_eventid *
	(*create_new_eventid_func)(struct PCP *,
				   const char *,
				   struct PCP_save_event *);
	void (*destroy_new_eventid_func)(struct PCP *,
					 struct PCP_new_eventid *);

	int (*commit_func)(struct PCP *, struct PCP_new_eventid *,
			   struct PCP_commit *);
	int (*book_func)(struct PCP *, struct PCP_new_eventid *,
			 struct PCP_commit *);

	int (*list_all_func)(struct PCP *, struct PCP_list_all *);
	int (*cancel_func)(struct PCP *, const char *, int *);
	int (*uncancel_func)(struct PCP *, const char *, int,
			     struct PCP_uncancel *);
	int (*delete_func)(struct PCP *, struct PCP_delete *);
	int (*retr_func)(struct PCP *, struct PCP_retr *);
	const char *(*errmsg_func)(struct PCP *);

	int (*acl_func)(struct PCP *, const char *, int);
	int (*listacl_func)(struct PCP *,
			    int (*)(const char *, int, void *),
			    void *);
} ;

/******** Generic functions ********/

#define pcp_errmsg(s) ( (*(s)->errmsg_func)(s))

#define pcp_noop(s)   ( (*(s)->noop_func)(s))

#define pcp_authtoken(s) ( (*(s)->authtoken_func)(s))
/* Return authentication token, after open */

#define pcp_cleanup(s) ( (*(s)->cleanup_func)(s))
/* Miscellaneous cleanup, should be called after login */

#define pcp_new_eventid(s,e,a) ( (*(s)->create_new_eventid_func)((s),(e),(a)))
/* Allocate a new eventid. (e) - old eventid we're updating, or NULL */

#define pcp_destroy_eventid(s,e) ( (*(s)->destroy_new_eventid_func)((s),(e)))
/* Destroy the new eventid */

/* Commit/book event */
#define pcp_commit(s,e,f) ( (*(s)->commit_func)((s),(e),(f)))
#define pcp_book(s,e,f) ( (*(s)->book_func)((s),(e),(f)))

/* List events */
#define pcp_list_all(s,f) ( (*(s)->list_all_func)((s),(f)))
#define pcp_retr(s,f) ( (*(s)->retr_func)((s),(f)))

/* Cancel/Uncancel/Delete events */
#define pcp_cancel(s, n, e) ( (*(s)->cancel_func)((s), (n), (e)))
#define pcp_uncancel(s, n, f, e) ( (*(s)->uncancel_func)((s), (n), (f), (e)))
#define pcp_delete(s, p) ( (*(s)->delete_func)((s), (p)))

/* Close the driver */
#define pcp_close(s) ((*(s)->close_func)((s)))

/* Set/Get ACL list */
#define pcp_acl(s,w,f) ((*(s)->acl_func)((s), (w), (f)))
#define pcp_list_acl(s,f,v) ((*(s)->listacl_func)((s), (f), (v)))
#define pcp_has_acl(s) ((s)->acl_func != 0)

#define PCP_ACL_MODIFY		1
#define PCP_ACL_CONFLICT	2
#define PCP_ACL_LIST		4
#define PCP_ACL_RETR		8
#define PCP_ACL_NONE		16

/* DRIVER: Simple directory-based storage, with dot-locking */

struct PCP *pcp_open_dir(const char *,	/* Directory name */
			 const char *);	/* User name */

/* DRIVER: establish a connection to a server. */

struct PCP *pcp_open_server(const char *,	/* userid */
			    const char *,	/* password */
			    char **);		/* Return parameter: err msg */

/*
** If pcp_open_server fails it returns NULL.  If errmsg arg is not null,
** it MAY be initialized to an error message (bad password, etc...)  A null
** errmsg indicates a connection failure (check errno).  The err msg buffer
** must be checked, and discarded with free().
*/

struct PCP *pcp_reopen_server(const char *,	/* userid */
			      const char *,	/* authtoken from prev open */
			      char **);		/* Return parameter: err msg */

/*
** DRIVER: open a proxy connection
*/

struct PCP *pcp_find_proxy(const char *,	/* userid */
			   const char *,	/* Cluster name */
			   char **);		/* Return parameter: err msg */

/*
** Must call pcp_set_proxy immediatelly after a succesfull return from
** pcp_find_proxy().
*/

int pcp_set_proxy(struct PCP *,
		  const char *);	/* proxy from */

/*
** Provide for localization of error messages.
**
** PCP_STRERROR defines a function that returns a description for a PCP
** error code.  This is done in an obvious way that allows gettext() to
** be used (hint: override PCP_ERRMSG).
*/

#ifndef PCP_ERRMSG
#define PCP_ERRMSG(s) (s)
#endif

#define PCP_STRERROR_N(n,s) if (i == n) return (PCP_ERRMSG(s));

#define PCP_STRERROR const char *pcp_strerror(int i) { \
	PCP_STRERROR_N(PCP_ERR_LOCK, "Unable to lock the calendar.") \
	PCP_STRERROR_N(PCP_ERR_CONFLICT, "Event conflict.") \
	PCP_STRERROR_N(PCP_ERR_EVENTNOTFOUND, "Event not found.") \
	PCP_STRERROR_N(PCP_ERR_EVENTLOCKED, "Event locked.") \
return (0); }

const char *pcp_strerror(int);

/*
** Default locale-specific generic functions.
** Should be overridden to return locale-specific strings.
*/

const char *pcp_am();	/* AM */
const char *pcp_pm();	/* PM */
const char *pcp_wdayname(unsigned);	/* Sun, Mon, Tue... */
const char *pcp_wdayname_long(unsigned); /* Sunday, Monday, ... */
const char *pcp_monthname(unsigned);	/* Jan, Feb, Mar... */
const char *pcp_monthname_long(unsigned); /* January, February... */
int pcp_wday(const char *);	/* Sun, Mon, Tue... -> 0..7, -1 if no match */ 
int pcp_month(const char *);	/* Ditto for Jan, Feb, Mar */

int pcp_fmttime(char *, size_t, time_t, int);	/* Format date+time */

#define FMTTIME_DATE	1
#define FMTTIME_TIME	2
#define FMTTIME_TIMEDROP 4

int pcp_fmttimerange(char *, size_t, time_t, time_t); /* Format time range */


/*-------------------------------------------------------------------------
** Parse a list of words into a time_t, i.e. "tomorrow" "8pm"...
**-------------------------------------------------------------------------*/

struct pcp_parse_datetime_info {
	const char *today_name;
	const char *tomorrow_name;
} ;

time_t pcp_parse_datetime(int *argn,
			  int argc,
			  char **argv,
			  struct pcp_parse_datetime_info *info);

/*-------------------------------------------------------------------------
** We already have starting/ending time.  Parse "until" day, and generate
** weekly events until the given day.
**-------------------------------------------------------------------------*/

int pcp_parse_datetime_until(time_t start, time_t end,
			     /* Parsed start/time times */

			     int *argn,
			     int argc,
			     char **argv,
			    
			     int recurring_time,

			     /* Callback function receives times */

			     int (*save_date_time)(time_t, time_t, void *),
			     void *voidfunc);

#define PCP_RECURRING_WEEKLY   0
#define PCP_RECURRING_MONTHLY  1
#define PCP_RECURRING_ANNUALLY 2

/*-------------------------------------------------------------------------
** Convert year/month/day to time_t (midnight-midnight)
**------------------------------------------------------------------------*/

int pcp_parse_ymd(unsigned, unsigned, unsigned, time_t *, time_t *);

/*-------------------------------------------------------------------------
** Convert yyyymmddhhmmss in GMT to time_t
**------------------------------------------------------------------------*/

time_t pcp_gmtime(int y, int m, int d, int hh, int mm, int ss);
time_t pcp_gmtime_s(const char *p);


void pcp_gmtimestr(time_t t, char *); /* time_t to yyyymmddhhmmss */

/* --- INTERNAL FUNCTIONS --- */

const struct PCP_event_time **pcp_add_sort_times(const struct PCP_event_time *,
						 unsigned);
int pcp_read_saveevent(struct PCP_save_event *, char *, int);

extern int pcp_mksocket(const char *, const char *);

void pcp_acl_name(int, char *);
int pcp_acl_num(const char *);

const char *pcpuid();
const char *pcpgid();

#endif
