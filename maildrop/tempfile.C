#include "config.h"
#include	"tempfile.h"
#include	"funcs.h"
#include	"mio.h"


TempFile::TempFile() :
#if	SHARED_TEMPDIR
		fp(0),
#endif
		filename(0), fd(-1), do_remove(0)
{
	constructed();
}

TempFile::~TempFile()
{
	destroying();
	Close();
	if (filename)
	{
		delete[] filename;
		filename=0;
	}
}

int TempFile::Open(const char *fname, int flags, mode_t mode)
{
	Close();
	name(fname);

	fd=mopen(fname, flags, mode);
	if ( fd < 0 )
		name(0);
	else
		descriptor(fd);
	return (fd);
}

#if SHARED_TEMPDIR

int TempFile::Open()
{
	Close();
	fp=tmpfile();
	if (fp == 0)	return (-1);
	fd=fileno(fp);
	return (fd);
}
#endif


void TempFile::name(const char *fname)
{
	do_remove=0;
	if (filename)
		delete[] filename;
	if (!fname)
	{
		filename=0;
		return;
	}

	filename=new char[strlen(fname)+1];

	if (!filename)	outofmem();
	strcpy(filename, fname);
	do_remove=1;
}

void TempFile::Close()
{
	if (fd >= 0)
	{
		close(fd);
		fd= -1;
	}

#if	SHARED_TEMPDIR
	if (fp)
	{
		fclose(fp);
		fp=0;
	}
#endif

	if (do_remove)
	{
		unlink(filename);
		do_remove=0;
	}
}

void	TempFile::cleanup()
{
	Close();
}

// When forked, the child process should just close the descriptor, do NOT
// remove the file!

void	TempFile::forked()
{
	do_remove=0;
	Close();
}
