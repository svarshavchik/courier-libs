#ifndef	tempfile_h
#define	tempfile_h


#include	"config.h"
#include	<sys/types.h>
#include	"exittrap.h"

#if	HAVE_FCNTL_H
#include	<fcntl.h>
#endif

#if	SHARED_TEMPDIR
#include	<stdio.h>
#endif

///////////////////////////////////////////////////////////////////////////
//
// We need to keep track of all temporary files we've opened, and close
// them if the process terminates.
//
// A TempFile object represents one temporary file currently in use.
// Open() method, if succesfull, returns a file descriptor, and saves
// the filename internally in the object.
// It is necessary to call Close() to close the descriptor (also saved
// internally) in order to mark the temporary file as no longer being
// in use.  Close() will automatically delete the file.
//
///////////////////////////////////////////////////////////////////////////

class TempFile : public ExitTrap {

protected:
	void	cleanup();
	void	forked();

#if	SHARED_TEMPDIR
	FILE	*fp;		/* tmpfile() output */
#endif
	char	*filename;
	int	fd;

	int	do_remove;
public:
	TempFile();
	~TempFile();
	int Open(const char *, int, mode_t=0666);
#if	SHARED_TEMPDIR
	int Open();
#endif

protected:
	void name(const char *);	// Partial initialization
	void descriptor(int fd_) { fd=fd_; }
public:
	void Close();
} ;

#endif
