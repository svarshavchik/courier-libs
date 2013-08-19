/*
** Copyright 2001 Double Precision, Inc.  See COPYING for
** distribution information.
*/


#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "pcp.h"
#if HAVE_DIRENT_H
#include <dirent.h>
#else
#define dirent direct
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif

#ifndef SOMAXCONN
#define SOMAXCONN 5
#endif

int pcp_mksocket(const char *dir, const char *sockname)
{
	int fd;
	const char *priority;
	DIR *dirp;
	struct dirent *de;
	char *p;
	struct  sockaddr_un skun;

	p=malloc(strlen(sockname)+sizeof("_PRIORITY"));
	if (!p)
	{
		perror("malloc");
		return (-1);
	}

	strcat(strcpy(p, sockname), "_PRIORITY");

	priority=getenv(p);
	free(p);
	if (!priority || !*priority)
		priority="50";

	/* Delete the previous socket */

	dirp=opendir(dir);
	while (dirp && (de=readdir(dirp)))
	{
		const char *n=de->d_name;

		while (*n && isdigit((int)(unsigned char)*n))
			++n;

		if (strcmp(n, sockname))
			continue;

		p=malloc(strlen(dir)+strlen(de->d_name)+2);
		if (!p)
		{
			perror("malloc");
			closedir(dirp);
			return (-1);
		}
		strcat(strcat(strcpy(p, dir), "/"), de->d_name);
		unlink(p);
		free(p);
	}
	if (dirp)
		closedir(dirp);

	fd=socket(PF_UNIX, SOCK_STREAM, 0);

        if (fd < 0)
	{
		fprintf(stderr, "ALERT: socket failed: %s\n", strerror(errno));
		return (-1);
	}

        skun.sun_family=AF_UNIX;
        strcpy(skun.sun_path, dir);
        strcat(skun.sun_path, "/");
        strcat(skun.sun_path, priority);
        strcat(skun.sun_path, sockname);

        if (bind(fd, (const struct sockaddr *)&skun, sizeof(skun)) ||
	    listen(fd, SOMAXCONN) ||
	    chmod(skun.sun_path, 0777) ||
	    fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        {
		fprintf(stderr, "ALERT: %s\n", skun.sun_path);
                close(fd);
                return (-1);
        }
        return (fd);
}
