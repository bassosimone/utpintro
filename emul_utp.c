/* emul_utp.c */

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
 * Emulate a subset of libutp API
 */

#define _BSD_SOURCE 1

#include <sys/types.h>
#include <sys/time.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

#include "utp.h"

#define SEQ_LT(__l, __r)        ((int16_t)((__l) - (__r)) < 0)
#define SEQ_LEQ(__l, __r)       ((int16_t)((__l) - (__r)) <= 0)
#define SEQ_GT(__l, __r)        ((int16_t)((__l) - (__r)) > 0)
#define SEQ_GEQ(__l, __r)       ((int16_t)((__l) - (__r)) >= 0)

#define PACKET_VER(__foo)       ((__foo) & 15)
#define PACKET_TYPE(__foo)      (((__foo) & 240) >> 4)

#define HEADER_LEN              20
#define PACKET_MAX              1380

#define TARGET                  100000.0        /* 100 ms */

#define DEBUGX(foo, ...)

struct Header {
        u_int8_t typever;
        u_int8_t ext;
        u_int16_t conn_id;
        u_int32_t microsec;
        int32_t cur_delay;
        u_int32_t wnd_size;
        u_int16_t seqno;
        u_int16_t ackno;
};

struct Packet {
        u_int attempts;
        byte *base;
        size_t len_payload;
        size_t len_total;
        byte *payload;
        struct timeval timestamp;
};

struct UTPSocket {

        struct UTPFunctionTable functions;
        void *opaque;
        struct sockaddr_storage peername;
        socklen_t peernamelen;
        SendToProc *sendto_wrapper;
        void *sendto_opaque;

        /*
         * UTP control block.
         */
        u_int32_t microsec;
        int32_t min_delay;
        int32_t wavg_extra_delay;
        u_int16_t rcv_nxt;
        long rttvar;
        struct Packet snd_buffer[65536];
        double snd_cwnd;
        u_int32_t snd_flight_size;
        u_int16_t snd_max;
        u_int16_t snd_nxt;
        struct timeval snd_timestamp;
        u_int16_t snd_una;
        long srtt;
};

struct Globals {
        u_int created;
        struct UTPSocket sock;
};

struct Globals GLOBALS;

static void *xmalloc(size_t size)
{
        void *ptr;

        ptr = malloc(size);
        if (ptr == NULL)
                errx(1, "out of memory");
        return (ptr);
}

struct UTPSocket *UTP_Create(SendToProc *sendto_wrapper,
                             void *sendto_opaque,
                             const struct sockaddr *sa, socklen_t salen)
{
        struct UTPSocket *sock;

        if (GLOBALS.created) {
                warnx("We can create only one socket");
                return (NULL);
        }
        GLOBALS.created = 1;

        sock = &GLOBALS.sock;

        /*
         * Keep this snippet in sync with UTP_IsIncomingUTP().
         */
        memset(sock, 0, sizeof(*sock));
        sock->sendto_wrapper = sendto_wrapper;
        sock->sendto_opaque = sendto_opaque;
        memcpy(&sock->peername, sa, salen);
        sock->peernamelen = salen;
        sock->rcv_nxt = 1;
        sock->snd_cwnd = 2.0 * PACKET_MAX;
        sock->snd_max = 1;
        sock->snd_nxt = 1;
        (void) gettimeofday(&sock->snd_timestamp, NULL);
        sock->snd_una = 1;

        return (sock);
}

void
UTP_SetCallbacks(struct UTPSocket *sock, struct UTPFunctionTable *func,
                 void *opaque)
{
        sock->functions = *func;
        sock->opaque = opaque;
}

void UTP_Connect(struct UTPSocket *sock)
{
        sock->functions.on_state(sock->opaque, UTP_STATE_CONNECT);  /* XXX */
}

static void send_packet(struct UTPSocket *sock, struct Packet *packet)
{
        struct Header *header;
        byte buffer[20];
        u_int32_t microsec;

        assert(sizeof(struct Header) == 20);
        assert(sizeof(buffer) >= sizeof(struct Header));

        (void) gettimeofday(&sock->snd_timestamp, NULL);
        microsec = (u_int32_t) (sock->snd_timestamp.tv_sec * 1000000
                                + sock->snd_timestamp.tv_usec);

        if (packet != NULL) {
                packet->timestamp = sock->snd_timestamp;
                packet->attempts += 1;
                header = (struct Header *) packet->base;
        } else
                header = (struct Header *) buffer;

        memset(header, 0, sizeof(*header));
        header->typever = 1;
        if (packet == NULL)
                header->typever += (2 << 4);
        header->ackno = htons(sock->rcv_nxt - 1);
        header->seqno = htons(sock->snd_nxt);
        header->microsec = htonl(microsec);
        if (sock->microsec > 0) {
                microsec -= sock->microsec;
                header->cur_delay = htonl(microsec);
        }

        DEBUGX(">>> TYPE %d USEC %d CUR_DELAY %d SEQNO %d ACKNO %d",
               PACKET_TYPE(header->typever), ntohl(header->microsec),
               ntohl(header->cur_delay), ntohs(header->seqno),
               ntohs(header->ackno));

        if (packet != NULL)
                sock->sendto_wrapper(sock->sendto_opaque, packet->base,
                                     packet->len_total,
                                     (struct sockaddr *) &sock->peername,
                                     sock->peernamelen);
        else
                sock->sendto_wrapper(sock->sendto_opaque, buffer, 20,
                                     (struct sockaddr *) &sock->peername,
                                     sock->peernamelen);
}

bool
UTP_IsIncomingUTP(UTPGotIncomingConnection *handle_accept,
                  SendToProc *sendto_wrapper, void *sendto_opaque,
                  const byte *buffer, size_t nbytes,
                  const struct sockaddr *sa, socklen_t salen)
{
        u_long acked;
        int32_t extra_delay;
        struct Header *header;
        double off_target;
        struct Packet *packet;
        const byte *payload;
        long rtt_delta;
        long rtt_sample;
        struct timeval tv;
        struct UTPSocket *sock;

        /*
         * Parse header.
         */

        assert(sizeof(struct Header) == 20);

        if (nbytes < 20) {
                warnx("packet too short");
                return (0);
        }

        header = (struct Header *) buffer;

        header->conn_id = ntohs(header->conn_id);
        header->microsec = ntohl(header->microsec);
        header->cur_delay = ntohl(header->cur_delay);
        header->wnd_size = ntohl(header->wnd_size);
        header->seqno = ntohs(header->seqno);
        header->ackno = ntohs(header->ackno);

        if (PACKET_VER(header->typever) != 1) {
                warnx("invalid packet version");
                return (0);
        }
        if (PACKET_TYPE(header->typever) > 4) {
                warnx("invalid packet type");
                return (0);
        }

        DEBUGX("<<< TYPE %d USEC %d CUR_DELAY %d SEQNO %d ACKNO %d",
               PACKET_TYPE(header->typever), header->microsec,
               header->cur_delay, header->seqno, header->ackno);

        nbytes -= sizeof(struct Header);
        payload = buffer + sizeof(struct Header);
        while (header->ext) {
                if (nbytes < 2) {
                        warnx("packet too short");
                        return (0);
                }
                nbytes -= 2;
                payload += 2;
                if (payload[-2] == 0)
                        break;
                if (nbytes < payload[-1]) {
                        warnx("packet too short");
                        return (0);
                }
                /* NOTE: SACKs are not yet implemented */
                nbytes -= payload[-1];
                payload += payload[-1];
        }

        /*
         * Pick up the right socket (simplified).
         */

        if (header->conn_id != 0) {
                warnx("we support the zero connection ID only");
                return (0);
        }
        sock = &GLOBALS.sock;

        if (!GLOBALS.created) {
                GLOBALS.created = 1;

                /*
                 * Keep this snippet in sync with UTP_Create().
                 */
                memset(sock, 0, sizeof(*sock));
                sock->sendto_wrapper = sendto_wrapper;
                sock->sendto_opaque = sendto_opaque;
                memcpy(&sock->peername, sa, salen);
                sock->peernamelen = salen;
                sock->rcv_nxt = 1;
                sock->snd_cwnd = 2.0 * PACKET_MAX;
                sock->snd_max = 1;
                sock->snd_nxt = 1;
                (void) gettimeofday(&sock->snd_timestamp, NULL);
                sock->snd_una = 1;

                if (handle_accept)
                        handle_accept(sendto_opaque, sock);
        }

        /*
         * Socket-specific code.
         */

        sock->microsec = header->microsec;      /* XXX */

        /* Sender */
        if (SEQ_GEQ(header->ackno, sock->snd_una) &&
            SEQ_LEQ(header->ackno, sock->snd_max)) {

                acked = 0;
                while (SEQ_LEQ(sock->snd_una, header->ackno)) {
                        packet = &sock->snd_buffer[sock->snd_una];
                        if (packet->base == NULL)
                                abort();

                        acked += packet->len_payload;
                        sock->snd_flight_size -= packet->len_payload;

                        if (packet->attempts == 1) {
                                (void) gettimeofday(&tv, NULL);
                                timersub(&tv, &packet->timestamp, &tv);
                                rtt_sample =
                                    tv.tv_usec + tv.tv_sec * 1000000;
                                rtt_delta = sock->srtt - rtt_sample;
                                if (rtt_delta < 0)
                                        rtt_delta = -rtt_delta;
                                sock->rttvar += ((rtt_delta
                                                  - sock->rttvar) >> 2);
                                sock->srtt += ((rtt_sample
                                                - sock->srtt) >> 3);
                        }

                        free(packet->base);
                        memset(packet, 0, sizeof(*packet));

                        sock->snd_una += 1;
                }
                if (acked == 0)
                        goto receiver;

                if (sock->min_delay == 0)
                        sock->min_delay = header->cur_delay;
                if (header->cur_delay < sock->min_delay)
                        sock->min_delay = header->cur_delay;

                extra_delay = header->cur_delay - sock->min_delay;
                off_target = 1 - extra_delay / TARGET;
                sock->snd_cwnd += (off_target * acked * PACKET_MAX)
                    / sock->snd_cwnd;

                if (sock->snd_cwnd < PACKET_MAX)
                        sock->snd_cwnd = PACKET_MAX;    /* XXX */

                sock->wavg_extra_delay = 0.2 * extra_delay +
                    0.8 * sock->wavg_extra_delay;

                if (sock->functions.on_state)
                        sock->functions.on_state(sock->opaque,
                                                 UTP_STATE_WRITABLE);
        } else {
                /* TODO: handle old/out-of-order ACKs */
        }

        /* Receiver */
      receiver:
        if (nbytes > 0 && PACKET_TYPE(header->typever) == 0) {
                /*
                 * TODO: store data in the reorder buffer and/or
                 * notify the receiver.
                 */
                if (header->seqno == sock->rcv_nxt)
                        sock->rcv_nxt += 1;

                send_packet(sock, NULL);        /* ACK */
        }

        return (1);
}

bool UTP_Write(struct UTPSocket *sock, size_t count)
{
        struct Packet *packet;
        size_t size;

        size = PACKET_MAX;
        if (count < size)
                size = count;

        /* Congestion control */
        if (sock->snd_flight_size + size > sock->snd_cwnd)
                return (0);

        if (SEQ_GT(sock->snd_nxt, sock->snd_max))
                sock->snd_max = sock->snd_nxt;

        packet = &sock->snd_buffer[sock->snd_nxt];
        memset(packet, 0, sizeof(*packet));
        packet->base = xmalloc(size + HEADER_LEN);
        packet->payload = packet->base + HEADER_LEN;
        packet->len_payload = size;
        packet->len_total = packet->len_payload + HEADER_LEN;

        if (sock->functions.on_write)
                sock->functions.on_write(sock, packet->payload,
                                         packet->len_payload);

        send_packet(sock, packet);

        sock->snd_flight_size += size;
        sock->snd_nxt += 1;

        return (1);
}

void UTP_CheckTimeouts(void)
{
        long elapsed;
        long rto;
        struct Packet *packet;
        struct UTPSocket *sock;
        double timestamp;
        struct timeval tv;

        sock = &GLOBALS.sock;

        /* Simplified RTO procedure */
        if (SEQ_GT(sock->snd_nxt, sock->snd_una)) {
                rto = sock->srtt + (sock->rttvar << 2);
                (void) gettimeofday(&tv, NULL);
                timersub(&tv, &sock->snd_timestamp, &tv);
                elapsed = tv.tv_usec + tv.tv_sec * 1000000;
                if (elapsed > rto) {
                        sock->snd_cwnd = 1.0 * PACKET_MAX;
                        sock->snd_flight_size = 0;
                        sock->snd_nxt = sock->snd_una;
                        /*
                         * REXMIT
                         */
                        warnx("REXMIT <%lf/%lf> %d", elapsed / 1000.0,
                              rto / 1000.0, sock->snd_nxt);
                        packet = &sock->snd_buffer[sock->snd_nxt];
                        if (packet->base == NULL)
                                abort();
                        send_packet(sock, packet);
                        sock->snd_flight_size += packet->len_payload;
                        sock->snd_nxt += 1;
                }
        }

        /* Stats for debugging */
        DEBUGX("[%d - %d] <%d / %.3lf> <%.3lf> <%.3lf; %.3lf>",
               sock->snd_una, sock->snd_nxt, sock->snd_flight_size,
               sock->snd_cwnd, sock->wavg_extra_delay / 1000.0,
               sock->srtt / 1000.0, sock->rttvar / 1000.0);

        /* Stats for plots */
        (void) gettimeofday(&tv, NULL);
        timestamp = tv.tv_sec + tv.tv_usec / 1000000.0;
        printf("%lf %d %.3lf %.3lf %.3lf %.3lf\n", timestamp,
               sock->snd_flight_size, sock->snd_cwnd,
               sock->wavg_extra_delay / 1000.0,
               sock->srtt / 1000.0, sock->rttvar / 1000.0);
        fflush(stdout);
}
