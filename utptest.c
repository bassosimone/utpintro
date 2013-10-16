/* utptest.c */

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
 * UTP test client and server.
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
#include "utp.h"

#define WRITESIZE_MAX           (1 << 20)

#define TIMEO_CHECK_INTERVAL    500000

struct TestContext {
        u_int lflag;
        struct UTPSocket *utpsock;
        size_t writesize;
        int sock;
};

static void _utp_read(void *, const byte *, size_t);
static void _utp_write(void *, byte *, size_t);
static size_t _utp_get_rb_size(void *);
static void _utp_state(void *, int);
static void _utp_error(void *, int);
static void _utp_overhead(void *, bool, size_t, int);

static void _utp_accept(void *, struct UTPSocket *);
static void _utp_sendto(void *, const u_char *, size_t,
                        const struct sockaddr *, socklen_t);

static void _libevent_readwrite(int, short, void *);
static void _libevent_quit(int, short, void *);
static void _libevent_timeo(int, short, void *);

struct UTPFunctionTable _UTP_CALLBACKS = {
        &_utp_read,
        &_utp_write,
        &_utp_get_rb_size,
        &_utp_state,
        &_utp_error,
        &_utp_overhead,
};

static void _utp_read(void *opaque, const byte *buf, size_t cnt)
{
        /* nothing */ ;
}

static void _utp_write(void *opaque, byte *buf, size_t cnt)
{
        memset(buf, 'A', cnt);
}

static size_t _utp_get_rb_size(void *opaque)
{
        return (0);             /* means: buffer empty */
}

static void _utp_state(void *opaque, int state)
{
        struct TestContext *context;

        context = (struct TestContext *) opaque;

        /*warnx("utp state: %d", state); */

        if (!context->lflag && (state == UTP_STATE_WRITABLE ||
                                state == UTP_STATE_CONNECT))
                while (UTP_Write(context->utpsock, context->writesize));
}

static void _utp_error(void *opaque, int error)
{
        warnx("utp socket error: %d", error);
}

static void
_utp_overhead(void *opaque, bool sending, size_t count, int type)
{
        /* nothing */ ;
}

static void _utp_accept(void *opaque, struct UTPSocket *utpsock)
{
        struct TestContext *context;

        context = (struct TestContext *) opaque;

        if (context->utpsock != NULL) {
                warnx("utp: ignore multiple accept attempts");
                return;
        }

        context->utpsock = utpsock;

        UTP_SetCallbacks(context->utpsock, &_UTP_CALLBACKS, context);
}

static void
_utp_sendto(void *opaque, const byte *pkt, size_t len,
            const struct sockaddr *sa, socklen_t salen)
{
        struct TestContext *context;
        ssize_t result;

        /* TODO: buffer and retry if sendto() fails? */

        context = (struct TestContext *) opaque;

        result = sendto(context->sock, (char *) pkt, len, 0,
                        (struct sockaddr *) sa, salen);

        if (result < 0)
                warn("sendto() failed");
}

static void _libevent_readwrite(int fd, short event, void *opaque)
{
        byte buffer[8192];
        struct TestContext *context;
        ssize_t result;
        struct sockaddr_storage sa;
        socklen_t salen;

        context = (struct TestContext *) opaque;

        if ((event & EV_READ) != 0) {
                memset(&sa, 0, sizeof(sa));
                salen = sizeof(sa);

                result = recvfrom(fd, (char *) buffer, sizeof(buffer),
                                  0, (struct sockaddr *) &sa, &salen);

                if (result > 0) {
                        (void) UTP_IsIncomingUTP(_utp_accept, _utp_sendto,
                            context, buffer, (size_t) result,
                            (const struct sockaddr *) &sa, salen);
                } else
                        warn("recvfrom() failed");
        }

        /* Note: At the moment we don't poll for EV_WRITE */
}

static void _libevent_timeo(int fd, short event, void *opaque)
{
        struct event *evtimeo;
        struct timeval tv;

        evtimeo = (struct event *) opaque;

        UTP_CheckTimeouts();

        memset(&tv, 0, sizeof(tv));
        tv.tv_usec = TIMEO_CHECK_INTERVAL;

        evtimer_add(evtimeo, &tv);
}

static void _libevent_quit(int fd, short event, void *opaque)
{
        exit(0);
}

#define USAGE \
    "usage: utptest [-B write-size] [-p local-port] address port\n" \
    "       utptest -l [-p local-port]\n"

int main(int argc, char *const *argv)
{
        int activate;
        struct event_base *base;
        struct TestContext context;
        const char *errstr;
        struct event evquit;
        struct event evreadwrite;
        struct event evtimeo;
        int opt;
        long long port;
        int result;
        struct sockaddr_storage salocal;
        struct sockaddr_storage saremote;
        struct sockaddr_in *sin;
        struct timeval tv;

        /*
         * Init.
         */

        memset(&context, 0, sizeof(context));
        context.writesize = 1380;
        context.sock = -1;

        base = event_init();
        if (base == NULL)
                errx(1, "event_init() failed");

        memset(&salocal, 0, sizeof(struct sockaddr_storage));
        sin = (struct sockaddr_in *) &salocal;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(54321);

        memset(&saremote, 0, sizeof(struct sockaddr_storage));
        sin = (struct sockaddr_in *) &saremote;
        sin->sin_family = AF_INET;

        /*
         * Process command line options.
         */

        while ((opt = getopt(argc, argv, "B:lp:")) >= 0) {
                switch (opt) {
                case 'B':
                        context.writesize = openbsd_strtonum(optarg,
                            0, WRITESIZE_MAX, &errstr);
                        if (errstr)
                                errx(1, "writesize is %s", errstr);
                        break;
                case 'l':
                        context.lflag = 1;
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
        if ((context.lflag && argc > 0) || (!context.lflag && argc != 2)) {
                fprintf(stderr, "%s", USAGE);
                exit(1);
        }

        if (!context.lflag) {
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

        context.sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (context.sock == -1)
                err(1, "socket() failed");

        result = evutil_make_socket_nonblocking(context.sock);
        if (result != 0)
                return (result);

        activate = 1;
        result = setsockopt(context.sock, SOL_SOCKET, SO_REUSEADDR,
                            &activate, sizeof(activate));
        if (result != 0)
                err(1, "setsockopt() failed");

        sin = (struct sockaddr_in *) &salocal;
        result = bind(context.sock, (struct sockaddr *) sin, sizeof(*sin));
        if (result != 0)
                err(1, "bind() failed");

        if (!context.lflag) {
                sin = (struct sockaddr_in *) &saremote;
                context.utpsock = UTP_Create(_utp_sendto, &context,
                                             (struct sockaddr *) sin,
                                             sizeof(*sin));
                if (context.utpsock == NULL)
                        errx(1, "UTP_Create() failed");
                UTP_SetCallbacks(context.utpsock, &_UTP_CALLBACKS,
                                 &context);
                UTP_Connect(context.utpsock);
        }

        /*
         * Create and dispatch the events.
         */

        event_set(&evreadwrite, context.sock, EV_READ | EV_PERSIST,
                  _libevent_readwrite, &context);
        event_add(&evreadwrite, NULL);

        if (!context.lflag) {
                evtimer_set(&evquit, _libevent_quit, NULL);
                memset(&tv, 0, sizeof(tv));
                tv.tv_sec = 180;
                evtimer_add(&evquit, &tv);
        }

        evtimer_set(&evtimeo, _libevent_timeo, &evtimeo);
        memset(&tv, 0, sizeof(tv));
        tv.tv_usec = TIMEO_CHECK_INTERVAL;
        evtimer_add(&evtimeo, &tv);

        warnx("libevent method: %s", event_base_get_method(base));

        event_dispatch();

        return (0);
}
