/*
 * radmin.c	RADIUS Administration tool.
 *
 * Version:	$Id$
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2008   The FreeRADIUS server project
 * Copyright 2008   Alan DeKok <aland@deployingradius.com>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include <freeradius-devel/libradius.h>
#include <freeradius-devel/radpaths.h>

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif


static int fr_domain_socket(const char *path)
{
        int sockfd;
	size_t len;
	socklen_t socklen;
        struct sockaddr_un saremote;

	len = strlen(path);
	if (len >= sizeof(saremote.sun_path)) {
		fprintf(stderr, "Path too long in filename\n");
		return -1;
	}

        if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Failed creating socket: %s\n",
			strerror(errno));
		return -1;
        }

        saremote.sun_family = AF_UNIX;
	memcpy(saremote.sun_path, path, len); /* not zero terminated */
	
	socklen = sizeof(saremote.sun_family) + len;

        if (connect(sockfd, (struct sockaddr *)&saremote, socklen) < 0) {
		fprintf(stderr, "Failed connecting to %s: %s\n",
			path, strerror(errno));
		close(sockfd);
		return -1;
        }

#ifdef O_NONBLOCK
	{
		int flags;
		
		if ((flags = fcntl(sockfd, F_GETFL, NULL)) < 0)  {
			fprintf(stderr, "Failure getting socket flags: %s",
				strerror(errno));
			close(sockfd);
			return -1;
		}
		
		flags |= O_NONBLOCK;
		if( fcntl(sockfd, F_SETFL, flags) < 0) {
			fprintf(stderr, "Failure setting socket flags: %s",
				strerror(errno));
			close(sockfd);
			return -1;
		}
	}
#endif

	return sockfd;
}

int main(int argc, char **argv)
{
	int sockfd, port;
	char *line;
	ssize_t len, size;
	const char *file = RUNDIR "/radiusd/radiusd.sock";
	char *p, buffer[2048];

	if ((argc > 2) ||
	    ((argc == 2) && (strcmp(argv[1], "-h") == 0))) {
		printf("Usage: radmin [socket]\n");
		exit(0);
	}

	if (argc == 2) file = argv[1];

#ifdef HAVE_READLINE_READLINE_H
	using_history();
	rl_bind_key('\t', rl_insert);
#endif

	/*
	 *	FIXME: Get destination from command line, if possible?
	 */
	sockfd = fr_domain_socket(file);
	if (sockfd < 0) {
		exit(1);
	}

	/*
	 *	FIXME: Do login?
	 */

	while (1) {
#ifndef HAVE_READLINE_READLINE_H
		fprintf(stdout, "radmin> ");
		fflush(stdout);

		line = fgets(buffer, sizeof(buffer), stdin);
		if (!line) break;

		p = strchr(buffer, '\n');
		if (!p) {
			fprintf(stderr, "line too long\n");
			exit(1);
		}

		*p = '\0';
#else
		line = readline("radmin> ");

		if (!line) break;
		
		if (!*line) {
			free(line);
			continue;
		}

		add_history(line);
#endif

		/*
		 *	Write the text to the socket.
		 */
		if (write(sockfd, line, strlen(line)) < 0) break;
		if (write(sockfd, "\r\n", 2) < 0) break;

		/*
		 *	Exit, done, etc.
		 */
		if ((strcmp(line, "exit") == 0) ||
		    (strcmp(line, "quit") == 0)) {
			break;
		}

		/*
		 *	Read the response
		 */
		size = 0;
		buffer[0] = '\0';

		port = 1;
		memset(buffer, 0, sizeof(buffer));

		while (port == 1) {
			int rcode;
			fd_set readfds;

			FD_ZERO(&readfds);
			FD_SET(sockfd, &readfds);

			rcode = select(sockfd + 1, &readfds, NULL, NULL, NULL);
			if (rcode < 0) {
				if (errno == EINTR) continue;

				fprintf(stderr, "Failed selecting: %s\n",
					strerror(errno));
				exit(1);
			}

			len = recv(sockfd, buffer + size,
				   sizeof(buffer) - size - 1, MSG_DONTWAIT);
			if (len < 0) {
				/*
				 *	No data: keep looping
				 */
				if ((errno == EAGAIN) || (errno == EINTR)) {
					continue;
				}

				fprintf(stderr, "Error reading socket: %s\n",
					strerror(errno));
				exit(1);
			}
			if (len == 0) break; /* clean close of socket */

			size += len;
			buffer[size] = '\0';

			/*
			 *	There really has to be a better way of
			 *	doing this.
			 */
			p = strstr(buffer, "radmin> ");
			if (p &&
			    ((p == buffer) || 
			     (p[-1] == '\n') ||
			     (p[-1] == '\r'))) {
				*p = '\0';

				if (p[-1] == '\n') p[-1] = '\0';

				port = 0;
				break;
			}
		}

		/*
		 *	Blank prompt.  Go get another line.
		 */
		if (!buffer[0]) continue;

		buffer[size] = '\0'; /* this is at least right */
		puts(buffer);
		fflush(stdout);
	}

	printf("\n");

	return 0;
}

