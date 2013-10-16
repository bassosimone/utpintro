/* tcptest.c */

/*-
 * Copyright (c) 2013
 *     Nexa Center for Internet & Society, Politecnico di Torino (DAUIN)
 *     and Simone Basso <bassosimone@gmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * TCP test client and server.
 */

#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "event.h"
#include "strtonum.h"

struct TestContext {
	unsigned char	buffer[65536];
	struct event	evreadwrite;
	int		sock;
	int		writesize;
};

static void		_libevent_accept(int, short, void *);
static void		_libevent_connect(int, short, void *);
static void		_libevent_quit(int, short, void *);
static void		_libevent_readwrite(int, short, void *);

static void
_libevent_readwrite(int fd, short event, void *opaque)
{
	struct TestContext *context;
	ssize_t		    result;

	context = (struct TestContext *) opaque;

	if ((event & EV_READ) != 0) {
		result = read(fd, context->buffer, sizeof (context->buffer));
		if (result == 0)
			goto err;
		if (result < 0)
			goto err1;
	}

	if ((event & EV_WRITE) != 0) {
		result = write(fd, context->buffer, context->writesize);
		if (result < 0)
			goto err1;
	}

	return;

err1:	warn("I/O error");
err:	close(fd);
	event_del(&context->evreadwrite);
	free(context);
}

static void
_libevent_accept(int fd, short event, void *opaque)
{
	struct TestContext	*context;
	socklen_t		 length;
	struct sockaddr_storage	 storage;
	int			 sock;

	memset(&storage, 0, sizeof (storage));
	length = sizeof (storage);
	sock = accept(fd, (struct sockaddr *) &storage, &length);
	if (sock < 0)
		goto err;

	context = calloc(1, sizeof (*context));
	if (context == NULL)
		goto err1;

	event_set(&context->evreadwrite, sock, EV_READ|EV_PERSIST,
		  _libevent_readwrite, context);
	event_add(&context->evreadwrite, NULL);

	return;

err1:	close(sock);
err:	warn("accept() failed");
}

static void
_libevent_connect(int fd, short event, void *opaque)
{
	struct TestContext	*context;
	socklen_t		 length;
	int			 res;
	struct sockaddr_storage	 storage;

	context = (struct TestContext *) opaque;

	/*
	 * See <http://cr.yp.to/docs/connect.html>.
	 */

	memset(&storage, 0, sizeof (storage));
	length = sizeof (storage);
	res = getpeername(fd, (struct sockaddr *) &storage, &length);
	if (res != 0) {
		if (errno == ENOTCONN || errno == EINVAL)
			(void)read(fd, context->buffer,
			    sizeof (context->buffer));
		warn("connect() failed");
		close(fd);
		return;
	}

	event_set(&context->evreadwrite, fd, EV_WRITE|EV_PERSIST,
		  _libevent_readwrite, context);
	event_add(&context->evreadwrite, NULL);
}

static void
_libevent_quit(int fd, short event, void *opaque)
{
	exit(0);
}

#define USAGE								\
    "usage: tcptest [-B write-size] [-p local-port] address port\n"	\
    "       tcptest -l [-p local-port]\n"

int
main(int argc, char *const *argv)
{
	int			 activate;
	struct event_base	*base;
	struct TestContext	 context;
	const char		*errstr;
	struct event		 evquit;
	uint8_t			 lflag;
	int			 opt;
	long long		 port;
	int			 result;
	struct sockaddr_storage	 salocal;
	struct sockaddr_storage	 saremote;
	struct sockaddr_in	*sin;
	struct timeval		 tv;

	/*
	 * Init.
	 */

	memset(&context, 0, sizeof (context));
	context.writesize = 1380;
	context.sock = -1;

	base = event_init();
	if (base == NULL)
		errx(1, "event_init() failed");

	memset(context.buffer, 'A', sizeof (context.buffer));

	memset(&salocal, 0, sizeof (struct sockaddr_storage));
	sin = (struct sockaddr_in *) &salocal;
	sin->sin_family = AF_INET;
	sin->sin_port = htons(54321);

	memset(&saremote, 0, sizeof (struct sockaddr_storage));
	sin = (struct sockaddr_in *) &saremote;
	sin->sin_family = AF_INET;

	/*
	 * Process command line options.
	 */

	lflag = 0;

	while ((opt = getopt(argc, argv, "B:lp:")) >= 0) {
		switch (opt) {
		case 'B':
			context.writesize = openbsd_strtonum(optarg,
			    0, sizeof (context.buffer), &errstr);
			if (errstr)
				errx(1, "writesize is %s", errstr);
			break;
		case 'l':
			lflag = 1;
			break;
		case 'p':
			sin = (struct sockaddr_in *) &salocal;
			port = openbsd_strtonum(optarg, 1024, 65535, &errstr);
			if (errstr)
				errx(1, "port is %s", errstr);
			sin->sin_port = htons((u_int16_t) port);
			break;
		default:
			fprintf(stderr, "%s", USAGE);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;
	if ((lflag && argc > 0) || (!lflag && argc != 2)) {
		fprintf(stderr, "%s", USAGE);
		exit(1);
	}

	if (!lflag) {
		sin = (struct sockaddr_in *) &saremote;
		result = inet_pton(AF_INET, argv[0], &sin->sin_addr);
		if (result != 1)
			errx(1, "inet_pton() failed");
		port = openbsd_strtonum(argv[1], 1024, 65535, &errstr);
		if (errstr)
			errx(1, "port is %s", errstr);
		sin->sin_port = htons((u_int16_t) port);
	}

	/*
	 * Create the socket.
	 */

	context.sock = socket(AF_INET, SOCK_STREAM, 0);
	if (context.sock < 0)
		err(1, "socket() failed");

	result = evutil_make_socket_nonblocking(context.sock);
        if (result != 0)
                return (result);

	if (lflag) {
		activate = 1;
		result = setsockopt(context.sock, SOL_SOCKET, SO_REUSEADDR,
				&activate, sizeof (activate));
		if (result != 0)
			err(1, "setsockopt() failed");

		sin = (struct sockaddr_in *) &salocal;

		result = bind(context.sock, (struct sockaddr *) sin,
			sizeof (*sin));
		if (result != 0)
			err(1, "bind() failed");

		result = listen(context.sock, 10);
		if (result != 0)
			err(1, "listen() failed");

	} else {
		sin = (struct sockaddr_in *) &saremote;
		result = connect(context.sock, (struct sockaddr *) sin,
				sizeof (*sin));
		if (result != 0 && errno != EINPROGRESS)
			err(1, "connect() failed");
	}

	/*
	 * Create and dispatch the events.
	 */

	if (lflag) {
		event_set(&context.evreadwrite, context.sock,
		    EV_READ, _libevent_accept, NULL);
		event_add(&context.evreadwrite, NULL);

	} else {
		event_set(&context.evreadwrite, context.sock, EV_WRITE,
			_libevent_connect, &context);
		event_add(&context.evreadwrite, NULL);
		/*
		 * Quit the program after 180 seconds.
		 */
		evtimer_set(&evquit, _libevent_quit, NULL);
		memset(&tv, 0, sizeof (tv));
		tv.tv_sec = 180;
		evtimer_add(&evquit, &tv);
	}

	warnx("libevent method: %s", event_base_get_method(base));

	event_dispatch();

	return (0);
}
