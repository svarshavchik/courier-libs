#ifndef	imapwrite_h
#define	imapwrite_h

#ifdef __cplusplus
/*
** Copyright 1998 - 2022 S. Varshavchik.
** See COPYING for distribution information.
*/

#include <string>

void writemailbox(const std::string &);
extern "C" {
#endif

void writeflush();
void writemem(const char *, size_t);
void writes(const char *);
void writeqs(const char *);
void writen(unsigned long n);
void write_error_exit(const char *);
#ifdef __cplusplus
}
#endif
#endif
