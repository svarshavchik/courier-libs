#ifndef	sqwebmail_pcp_h
#define	sqwebmail_pcp_h

/*
*/

#include	"config.h"

struct PCP;

extern void sqpcp_init();	/* CGI startup */
extern void sqpcp_close();	/* CGI cleanup */

extern void sqpcp_login(const char *, const char *);	/* Login */

extern int sqpcp_loggedin();	/* Return non-zero if logged in */

int sqpcp_has_calendar();	/* Non zero if calendaring is enabled */
int sqpcp_has_groupware();	/* Non zero if groupware is enabled */

void sqpcp_summary();		/* Print summary in folders.html */

struct PCP *sqpcp_calendar();

void sqpcp_newevent();	/* Event update procedure */

void sqpcp_eventstart();	/* Begin displaying event information */
void sqpcp_eventfrom();		/* Display From: header */
void sqpcp_eventtimes();	/* Display event times */
void sqpcp_eventparticipants();	/* Display event participants */
void sqpcp_eventtext();		/* Display event text */
void sqpcp_eventattach();	/* Attachment list */
void sqpcp_eventend();		/* End displaying event information */

/* Attachment stuff */

void sqpcp_deleteattach();
void sqpcp_uploadattach();
void sqpcp_attachpubkey();
void sqpcp_attachprivkey();

void sqpcp_preview();		/* Preview event */
void sqpcp_save();		/* Save event */
void sqpcp_postpone();		/* Postpone event */

/* Daily calendar */

void sqpcp_todays_date();
void sqpcp_todays_date_verbose();
void sqpcp_daily_view();
void sqpcp_prevday();
void sqpcp_nextday();
void sqpcp_weeklylink();
void sqpcp_monthlylink();

/* Weekly calendar */

void sqpcp_show_cal_week();
void sqpcp_show_cal_nextweek();
void sqpcp_show_cal_prevweek();
void sqpcp_displayweek();

/* Monthly calendar */

void sqpcp_show_cal_month();
void sqpcp_show_cal_nextmonth();
void sqpcp_show_cal_prevmonth();
void sqpcp_displaymonth();

/* Display event */

void sqpcp_displayeventinit();
void sqpcp_displayevent();
void sqpcp_eventbacklink();
void sqpcp_eventeditlink();

int sqpcp_eventedit();

void sqpcp_eventcanceluncancellink();
void sqpcp_eventcanceluncancelimage();
void sqpcp_eventcanceluncanceltext();
void sqpcp_eventdeletelink();
void sqpcp_deleteeventinit();
void sqpcp_dodelete();
void sqpcp_eventacl();
#endif
