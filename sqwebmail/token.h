/*
*/
#ifndef	token_h
#define	token_h

/*
** Copyright 1998 - 1999 Double Precision, Inc.  See COPYING for
** distribution information.
*/


/* To keep a reload from executing a duplicate operation -- such as sending
** a message, each form generates a unique token, which after the operation
** is complete gets saved in MSGTOKENFILE.  Before starting the operation,
** MSGTOKENFILE is checked, and if it has our token, the operation is
** skipped.
*/

void tokennew();
void tokennewget();
int tokencheck();
void tokensave();

#endif
