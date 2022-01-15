#ifndef	mysignal_h
#define	mysignal_h

#ifdef __cplusplus
extern "C" {
#endif
/*
** Copyright 1998 - 1999 S. Varshavchik.
** See COPYING for distribution information.
*/

void trap_signals();
int release_signals();
#ifdef __cplusplus
}
#endif

#endif
