# The Micro Transport Protocol

*A lecture for the Streaming Multimediale course*

- *Author:* Simone Basso &lt;bassosimone@gmail.com&gt;
- *Date:* 2013-11-07
- *License:* [Creative Commons BY 3.0 Unported][cc-by]
- *Version:* 0.0.4

[cc-by]: http://creativecommons.org/licenses/by/3.0/
![CC BY 3.0 Unported logo][cc-by-logo]
[cc-by-logo]: http://i.creativecommons.org/l/by/3.0/88x31.png

## 1. Introduction

This lecture is about the *micro transport protocol* (uTP), which
is an [application layer] protocol that works on top of the [user
datagram protocol (UDP)][udp].  The [congestion control] algorithm
at the hearth of uTP is called *Low Extra Delay Background Transport*
(LEDBAT). The uTP and LEDBAT are documented by the [BitTorrent
extension proposal (BEP) 29][bep29], and LEDBAT is also documented
by [RFC6817][rfc6817]; both documents were written by [BitTorrent]
developers, who were trying to solve a specific problem: the
*bufferbloat* caused by BitTorrent.

[bep29]: http://www.bittorrent.org/beps/bep_0029.html
[application layer]: http://en.wikipedia.org/wiki/Application_layer
[congestion control]: http://en.wikipedia.org/wiki/Network_congestion
[udp]: http://tools.ietf.org/html/rfc768
[rfc6817]: http://tools.ietf.org/html/rfc6817
[BitTorrent]: http://bittorrent.org/

The [bufferbloat] &mdash; which, to be clear, is not a BitTorrent-only
phenomenon &mdash; is the self-inflicted amount of *extra* [queuing delay] at
the bottleneck (often the gateway) caused by the interaction between
(a) the [transmission control protocol (TCP)][tcp] dynamics, (b) large
buffers in the routers, and (c) slow uplinks (e.g., the uplinks of
ADSL connections).

[bufferbloat]: http://en.wikipedia.org/wiki/Bufferbloat
[queuing delay]: http://en.wikipedia.org/wiki/Queuing_delay
[tcp]: http://en.wikipedia.org/wiki/Transmission_Control_Protocol

The structure of this document is as follows.  [To start
off](#2-the-bufferbloat) I define the bufferbloat and discuss a
simple do-it-yourself (DIY) experiment that I performed from
my home network to reveal the bufferbloat caused by TCP.
[Then](#3-ledbat) I describe LEDBAT, and I re-run the DIY
experiment using uTP, to show that uTP mitigates the bufferbloat.
On the go I also mention a couple of interesting research threads,
including the one about the bad interplay between LEDBAT
and router-level solutions for mitigating the bufferbloat (such
as the [CoDel] packet-scheduling algorithm). [Finally](#4-utp)
I describe how the LEDBAT congestion control algorithm is
implemented into uTP.

[CoDel]: http://en.wikipedia.org/wiki/CoDel

## 2. The bufferbloat

Some researchers have been claiming for a long time that [latency
is the parameter that affects the network performance and the quality
of experience (QoE) the most][latency-rant]. With the growth of the
buffers in home routers the problem of high latency has now become
mainstream, to the point that [we have a name for the excess of
buffering available at the bottleneck][gettys2012] (which typically
is the home router): *bufferbloat*.

[latency-rant]: http://rescomp.stanford.edu/~cheshire/rants/Latency.html
[gettys2012]: http://cacm.acm.org/magazines/2012/1/144810-bufferbloat/fulltext

The problem with large buffers is that typically TCP uses the packet
losses as the only signal of congestion (and as an indication that
it should slow down). Therefore, when you have more data to send
than the home router can handle and provided that the router [does
not actively manage the queue][aqm], TCP fills large the router's buffer,
then slows down, then fills the large buffer again, then slows down,
and so on and so forth.

[aqm]: http://en.wikipedia.org/wiki/Active_queue_management

Moreover, because the uplink is slow, the forwarding of each packet
takes a relatively high amount of time (for example, I estimated
that my 476 Kbit/s home router takes approx. 25 ms to send a 1,500
bytes packet upstream); therefore, when the TCP dynamics creates
long queues, the user will experience large delays.

### 2.1. The DIY experiment

To help you better understand the bufferbloat, I have run the experiment
described by Fig 1.

![Experiment scenario][scenario]
[scenario]: https://raw.github.com/bassosimone/utpintro/master/img/scenario.png

**Fig. 1** The DIY experiment scenario.

I have run a single-flow TCP upload from my Mac mini to one of my
servers at Politecnico di Torino.  During the experiment the Mac
mini was associated with the 11 Mbit/s WiFi network managed by my
home router. In turn the home router was attached to a
476 Kbit/s ADSL uplink (we don't care much about the downlink in
this experiment).  Both the home router and the Politecnico di
Torino Local Area Network (LAN) that hosts my servers were attached
to 'the Internet' (which includes my ISP network, a portion of
the backbone, and the campus network of Politecnico di Torino).

For discussing this experiment, we can consider 'the Internet' (as
defined above) to be a high quality network with low delay and
low packet loss rate.  Of course this description is over simplified,
however it is a sound simplification because the characteristics of
'the Internet' (latency, bandwidth, typical packet loss rate) were
much better than the ones of my uplink.

To understand the effect of the upload on the [Quality of Experience
(QoE)][QoE] of an user, I also run a ping(8) session from my Mac mini to
8.8.8.8, which is the anycast IPv4 address of Google's public DNS.
It is over-simplistic to pretend that the round trip time (RTT)
measured by ping(8) conveys the QoE; still the variation of the RTT
reported by ping(8) is an indirect measurement of what happens
inside the queue (which is indeed the starting point to reason about
the QoE).

[QoE]: http://en.wikipedia.org/wiki/Quality_of_experience

To measure the baseline RTT (i.e., the RTT when there is no competing
traffic), I started the ping(8) session before I started the upload,
and I terminated the ping(8) session after the upload was finished.

### 2.2. The Experiment Results

Fig. 2 shows the result of the DIY experiment. On the
X axis you see the elapsed time since I started the ping(8) session.
On the Y axis you see the RTT reported with ping(8), measured in
ms.

![Experiment results][ping-tcptest]
[ping-tcptest]: https://raw.github.com/bassosimone/utpintro/master/img/ping_tcptest.png

**Fig. 2** The RTT measured with 'ping 8.8.8.8' versus the
elapsed time since the beginning of the experiment.

Fig. 2 tells us that there is a big buffer into my home router,
which has room for about one and a half seconds of queue. Before
the upload starts, in fact, the RTT reported by ping(8) is consistently
below 100 ms. Conversely during the experiment the order of
magnitude of the experiment is one second.

It is also interesting to note that the RTT reported by ping(8)
follows a sawtooth pattern, which resembles the sawtooth pattern
of the TCP congestion window (cwnd) when TCP is in Congestion
Avoidance. It is not by chance that the ping(8) moves like the
congestion window of TCP; in fact, as we see in the next section,
the queue length is driven by the dynamics of TCP.

### 2.3. Discussion

Fig. 3 shows a more complex model of the DIY experiment scenario
in which we explicitly indicates the queues. By looking at the
speed at which queues are filled and drained, we noticed that the
ADSL router's queue is the queue that is more likely to be full
during the experiment.

![Experiment results][queues]
[queues]: https://raw.github.com/bassosimone/utpintro/master/img/queues.png


**Fig. 3** The experiment scenario with explicit indication of
the queues: the ADSL router uplink queue is clearly the
bottleneck.

In fact, the application is sending at maximum speed, therefore the
kernel's queue is likely to be always non-empty. As a consequence,
the kernel is sending packets at the maximum speed allowed by my
WiFi network (11 Mbit/s). In turn the router receives packets from
the WiFi network and fills its uplink queue, which is, alas, drained
at 476 Kbit/s only.

Once packets leave the uplink they enter 'the Internet', in which we
have no delay and bandwidth problems, as we said. Then they reach
the Politecnico di Torino network, in which, again, a flow at
476 Kbit/s (peak) does not create any performance issue.
Therefore packets arrive quite easily at the receiver, which generates
ACKs that flow back to the sender and tell the sender that it can
inject more packets into the network.

So, the rate at which the receiver generates ACKs is the rate at which
the router's queue is drained, because (as a first approximation)
TCP generates an ACK per received packet. From the [queueing theory] we
know that, when the rate at which we fill the queue equal the rate at
which we empty the queue, [the queue eventually fills][mm1].

[queueing theory]: http://en.wikipedia.org/wiki/Queueing_theory
[mm1]: http://en.wikipedia.org/wiki/M/M/1_queue#Stationary_analysis

When the queue at the router is full, packets sent on the WiFi
network are discarded. Now, when there
are losses, we have two cases: fast recovery and timeout.
In the fast recovery case (which happens when some packets after
the lost ones arrive at the receiver and allow the receiver to
generate some duplicate ACKs) the sender notices the losses and
halves the congestion window (i.e., the instantaneous send speed).
Therefore the queue length decreases, because the rate at which
we fill it is smaller than the rate at which we drain it.
In the timeout case the sender pauses for a while, therefore the
queue length decreases much faster, because the speed at which
we fill it is zero for a retransmit timeout (which in our
case is probably around one second).

In conclusion it should hopefully be clear now why the RTT
measured with ping(8) follows a pattern that resembles
the congestion window of TCP.

### 2.4. QoE discussion

Now that we know the amount of bufferbloat (about one second) and
we have a better understanding of the mechanism that causes it,
we can reason a bit on the effect of one-second extra-delay on the
QoE of many kinds of applications.

**DNS**: In the best case the time to resolve a domain name is higher
than one second, while in the worst case the packet is lost and
more time is needed.  Also, if too many DNS request packets are
lost, the resolve may decide that the server is not reachable and
may give up.  Even if the resolver does not give up, still there
is on the average one extra second between when you type 'facebook.com'
in the browser and when your browser consider connecting to the
closest facebook.com HTTP router.

**Web browsing**: the three-way handshake is slow, because the SYN
packet takes one extra second to reach the other endpoint, and,
similarly, the ACK after the SYN|ACK takes (on the average) one
extra second as well. Even if there are no losses, in which case
the delay only becomes higher, the time to fetch all the small
objects that compose a web page is very high, because each request
stays, on the average, one extra second idle in the router's queue.

**Video streaming**: for video streaming the QoE reduction is
annoying as well. Provided, in fact, that one manages to resolve
the domain names and manages to establish a connection, the initial
phase in which the video is buffered is slow, because the goodput
of TCP is inversely proportional to the RTT (which is higher because
of the queing delay). Also, if the server uses dynamic rate adaptation,
it may decide that the client is too slow for a high quality video,
and it may serve lower-quality frames.

**VoIP**: provided that it manages to work, the high latency is
terrible, because it makes an interactive communication very
problematic. The ITU, in fact, recommends in G.114 that the total
one-way delay shall be lower than 150 ms to have an acceptable
interactive communication.

### 2.5. Innovation at the edges

Now that we have discussed the effect of the bufferbloat on the
QoE, we can spend a few paragraph to understand the design problems
that cause the bufferbloat, and the related potential fixes.

We have two design problems: one is that the buffers are too large
and the network is not able to manage them efficiently; the other
is that the application that creates the bufferbloat (typically
BitTorrent) has background requirements (i.e., its traffic should
yield to more foreground traffic like web browsing, streaming,
and so forth, not vice-versa).

Therefore there are two possible solutions to the bufferbloat
problem: one in the network, which requires to manage the
queues in a better way, and one in the application, which requires
the application to implement better congestion control
algorithms.

Let's discuss the network side first.

Active queue management has been around for quite some time now,
and recently a very effective no-knobs queue management algorithm,
CoDel (for Controlled Delay), was proposed by Van Jacobson, et al.

Yet, to date, the router manufacturers ship routers that have
pre-CoDel AQM algorithms disabled by default. If they want to
mitigate the bufferbloat, in other words, users need to know very
well what to do.

I can't predict whether CoDel will be so good that it reverses the
trend, however so far the trend is that router manufacturers do
not enable AQM by default because that would involve making choices
on behalf of the users without knowing clearly what the users
preferences are.

On the contrary, since BitTorrent is an application with very clear
requirements and with attached certain user expectations (e.g.,
that it slows down the background download if I decide to see
a video streaming right now), probably it was in a better position
to judge the requirements and the needs.

(Also probably BitTorrent had more incentives: I doubt that one
blames the router for the bufferbloat, when he/she can blame
his/her BitTorrent for that.)

In my opinion this is a typical Internet story of innovation
without permission, in which innovation is more likely to
happen at the edges, where it is easier to experiment, where
the user requirements are more clear and less complex.

Of course other will have for sure other points of view: the
debate on whether innovation should happen in a possibly
chaotic way at the edge or in a more controlled and rational
way at the core is old as the Internet.

## 3. LEDBAT

This lecture comes just after you have extensively reviewed TCP,
therefore it makes sense to present LEDBAT by stressing the differences
between LEDBAT and TCP.

LEDBAT is a window-based congestion control algorithm that uses a
congestion window (cwnd) to control the amount of traffic injected
into the network, just like TCP. Also LEDBAT uses sequence numbers
and ACK numbers.

However, while TCP objective is to keep the optimal number of packets
in flight, LEDBAT aims to control the queue at the bottleneck.

The behavior of TCP (which strives to keep the optimal number of
packets in flight) is also called *self-clocking*, because the ACKs
flowing from the receiver to the sender are like a clock that the
sender uses to decide when to inject more packets into the network.
This behavior is depicted in Fig. 4 and is described in the "Congestion
Avoidance and Control" work by Jacobson [JAC90].

![Self clocking][self-clock]
[self-clock]: https://raw.github.com/bassosimone/utpintro/master/img/clock.png


**Fig. 4** The self-clocking behavior of TCP at the equilibrium.

The LEDBAT goals are clearly spelled out in its acronym:

**Low Extra Delay**: LEDBAT aims to control the extra delay on the
bottleneck queue to mitigate the bufferbloat.

**Background**: LEDBAT is designed for background traffic (BitTorrent
but possibly also backups), therefore, when there is contention
for the bottleneck, it should yield the bandwidth to TCP.

**Transport**: LEDBAT is a reliable protocol like TCP.

The remainder of the discussion on LEDBAT is structured as follows: we
start by describing the behavior of LEDBAT when there are no losses,
then we describe how LEDBAT behaves when it senses losses, after that
we describe the mechanism used by LEDBAT to measure the one-way
delay, then we repeat the DIY experiment using a LEDBAT implementation
to show that LEDBAT mitigates the bufferbloat, finally we briefly
describe some LEDBAT- and bufferbloat-related research topics.

### 3.1. Delay-based Congestion Control

When there are no losses the LEDBAT algorithm seeks to control the
queue length at the bottleneck (which is assumed to be the home
router) and hence the bufferbloat.

To control the queue length, LEDBAT needs to measure the amount of
*extra delay* (i.e., the additional queuing delay) that it generates.
LEDBAT, in fact, reduces its congestion window (cwnd) when there is
too much extra delay, and otherwise increases it, just like TCP.

To measure the extra delay, LEDBAT uses the ACKs: each ACK, in fact,
carries a measurement of the current one-way delay (henceforth
*current delay*) of the path.  Also LEDBAT tracks of the minimum
one-way delay, which is called the *base delay*.  The difference
between the current and the base delay is precisely the extra delay.

Also LEDBAT defines the *target delay*, which is the maximum amount
of extra delay that it should tolerate. According to the RFC the
target delay must not be larger than 100 ms.

Note that LEDBAT uses the one-way delay (as opposed to the RTT) to
take congestion control decisions (i.e., to inflate or deflate the
cwnd), because it wants to correlate more tightly the adjustment
of the cwnd with the delay on the bottleneck. An increase of the
delay of the return path, in fact, shall not cause the protocol to
reduce its cwnd.

Also the 100 ms is a compromise between the performance of LEBDAT
and the QoE of VoIP and other interactive applications. 100 ms, in
fact, is an amount of delay that does not make VoIP impossible
(since the ITU recommends to keep the one-way delay below 150 ms
for an acceptable interactive voice communication [G114][g114]).

[g114]: http://en.wikipedia.org/wiki/G.114

The following pseudocode shows a very-simplified form of the algorithm
run by LEDBAT when it receives an in-sequence ACK:

    #define TARGET_DELAY 100.0

    current_delay = ack.delay;
    base_delay = min(base_delay, current_delay);
    extra_delay = current_delay - base_delay;

    off_target = 1 - extra_delay / TARGET_DELAY;
    cwnd += off_target / cwnd;

Basically, LEDBAT computes the relative error between the extra delay
and the target delay and uses such error to inflate or deflate the
congestion window. Also, because the base delay is the minimum
between the base delay itself and the current delay, the extra delay
cannot be negative, therefore off_target cannot be greater than
one. This means that the congestion window of LEDBAT cannot grow
faster than 1 / cwnd (which is the rate at which the congestion
window grows with TCP in congestion avoidance).

Also, as the extra delay grows and comes closer to the TARGET_DELAY,
the off_target value becomes smaller and gets closer to zero, i.e.,
LEDBAT inflates its congestion window less aggressively then TCP.

Instead, when the extra delay is greater than the target delay, the
off_target variable is negative, and LEDBAT shrinks its cwnd. In
this case, however, there is no lower bound for the value of
off_target, therefore the cwnd is deflated proportionally to the
difference between the extra delay and the target.

#### 3.1.1. QoE Discussion

LEDBAT is never more aggressive than TCP, therefore it should never
cause more harm to the QoE than TCP.

Moreover, the extra delay added by a LEDBAT connection should be
around 100 ms, while we observed more than one second of extra
delay caused by a TCP flow.

Therefore, video streaming, browsing, and DNS should work better
when they compete with LEDBAT than when they compete with TCP.

VoIP is more critical, still a QoE of around 100 ms should, at
least in principle, make the VoIP possible.

However, the 100 ms is a compromise, and it does not fit any possible,
scenario, e.g., 100 ms of extra delay is probably a problem when you
are playing World of Warcraft.

### 3.2. LEDBAT on losses

In case of packet losses, LEDBAT behaves like TCP, i.e.:

- it performs the Fast Retransmit and the Fast Recovery when it
  receives the triple duplicate ACK, which basically means that LEDBAT
  halves its cwnd in case of occasional losses;

- when the Selective ACKs (SACKs) indicate that three packets were
  lost, LEDBAT triggers the Fast Retransmit and Recovery as well;

- also, LEDBAT uses the timeout as extrema ratio: if the ACK for a
  sent packet does not arrive before a retransmit timeout time,
  LEDBAT shrinks the congestion window to one packet.

The SACKs are a mechanism that allow the receiver to provide to the
sender a more clear picture of what was and of what was not lost
in case of losses.

By knowing which packets were lost and which packet arrived at the
receiver, the sender has a more precise picture of the amount of
packets in flight; also, the sender knows which packets should and
which packets should not be retransmitted, therefore the sender
can avoid more unneeded retransmissions.

The formula to compute the retransmit timeout used by LEDBAT is the
same one that TCP uses; the only major exception is that the timeout
of LEDBAT cannot be smaller than 0.5 s:

    delta = rtt - packet_rtt;
    rtt_var += (abs(delta) - rtt_var) / 4;
    rtt += (packet_rtt - rtt) / 8;
    rto = max(rtt + rtt_var * 4, 500);

The fact that the timeout is capped to 0.5 s means that LEDBAT is
less aggressive in retransmitting in case of timeout.

Fig. 5 shows a possible simplified finite state machine for LEDBAT in
which the cwnd is expressed in packets. There is no slow start, because
the slow start is aggressive, and LEDBAT has no aggressive goals. Also,
the finite state machine shows a simplified recovery in which the cwnd
is halved just after the third duplicate ack (or SACK notification).

![LEDBAT FSM][ledbat-fsm]
[ledbat-fsm]: https://raw.github.com/bassosimone/utpintro/master/img/fsm.png

**Fig. 5** A possible LEDBAT finite state machine.

### 3.3. One-way delay measurements

As we said, each ACK carries a measurement of the current one-way delay. Now,
we see how LEDBAT manages to include a measurement of the current delay in
each ACK.

First, each packet that LEDBAT send includes a timestamp from the sender. For
example, in Fig. 6, 10.0.0.1 sends a packet at time *t = 10* and includes
such timestamp in the packet. In the following, we call this first packet that
10.0.0.1 sends to 10.0.0.2 *pkt1*.

Then, the packet arrives at 10.0.0.2, which computes the difference between
the 10.0.0.1 timestamp and the current time. Note that, in the common case, the
10.0.0.1's and the 10.0.0.2's clocks are no in sync, therefore, the difference
is not the time that the packet was in flight. For this reason, in the following
we indicate the 10.0.0.1 clock using *t* and the 10.0.0.2 clock using *t'*.
Also, we indicate the (unknown) difference between the two clocks *t' - t* as
*skew*.

So, the diference between the time when the packet was received and the
time when the packet was sent is the flight time plus *skew*. For example,
in Fig. 6, the difference is 67, because 10.0.0.2 receives the packet at
a time *t' = 77*.

Later (not necessarily immediately) 10.0.0.2 sends a packet back to
10.0.0.1 (which includes an ACK and possibly some data). This packet
carries the sender time (*t' = 80* in Fig. 6) and also carries
the latest difference (*67* in Fig. 6).

When it gets the packet, 10.0.0.1 processes the difference sample sent
by 10.0.0.2 (i.e., *67*). Such sample is the current value of the one-way
delay in the *10.0.0.1 -> 10.0.0.2* direction, which we called the
*current delay* throughout these notes.

If the current delay is lower than the minimum delay, 10.0.0.1 updates
the minimum delay. Also, it computes the current extra delay, as we
have seen before, and it consequently updates its CWND. For example,
in Fig. 6, 10.0.0.1 later receives a packet that carries a current
delay sample that is *69*, from which 10.0.0.1 infers that the current
delay increased of two time units.

<center>

![LEDBAT timestamps][ledbat-timestamps]
[ledbat-timestamps]: https://raw.github.com/bassosimone/utpintro/master/img/timestamps.png

**Fig. 6** The travel of timestamp and timestamp_difference samples
from the two LEDBAT peers (assuming they use uTP).

</center>

Note that the difference between two current delay samples eliminates
the skew, in fact, if we call *pkt3* the second packet that 10.0.0.1
sends to 10.0.0.2, the following holds:

    69 = t'[pkt3_recv] - t[pkt3_send]
       = t[pkt3_recv] + skew - t[pkt3_send]
       = flight_pkt3 + skew

    67 = t'[pkt1_recv] - t[pkt1_send]
       = t[pkt1_recv] + skew - t[pkt1_send]
       = flight_pkt1 + skew

    69 - 67 = flight_pkt3 + skew - flight_pkt1 - skew
            = flight_pkt3 - flight_pkt1

So, we have seen how LEDBAT measures the current delay and how it
computes the timestamp difference, which is an estimate of the extra
delay. However, this explanation was a bit simplified and there are,
in fact, some complications:

1. in general (and especially for long-lived connections) the base
delay is not constant, but varies over time. To make an example, the
base delay may change dramatically if the route between the two
hosts change (perhaps because there was a link failure). Therefore,
LEDBAT computes the base delay over a reasonable time window, e.g., the
last two minutes;

2. the timestamp difference is noisy, therefore, there are arguments
in favor of filtering it, to reduce the noise (e.g., one can use
an EWMA);

3. the clock skew is typically not a constant function, but there
is a drift by which the two clocks diverge.

We don't dive into how LEDBAT addresses the problems 1-3 above in this
notes; if you are curious, refer to RFC 6817.

### 3.4. DIY experiment with LEDBAT/uTP

TODO

![ping(8) comparison][ping-comparison]
[ping-comparison]: https://raw.github.com/bassosimone/utpintro/master/img/ping_comparison.png

**Fig. 7** The RTT measured with ping(8) versus the elapsed time
since the beginning of the experiment for the TCP and uTP experiments
(the two experiments were run independently, but I put them
together in this plot to show that there is an order of magnitude
between the TCP and the uTP extra delay).

![ping(8) of the utptest][ping-utptest]
[ping-utptest]: https://raw.github.com/bassosimone/utpintro/master/img/ping_utptest.png


**Fig. 8** The RTT measured with ping(8) versus the elapsed time
since the beginning of the libutp experiment.

![ping(8) of the emul_utp][ping-emul-utp]
[ping-emul-utp]: https://raw.github.com/bassosimone/utpintro/master/img/ping_utptest_emul.png


**Fig. 9** The RTT measured with ping(8) versus the elapsed time
since the beginning of the emul_utp experiment.

![emul_utp internals: cwnd][internals-cwnd]
[internals-cwnd]: https://raw.github.com/bassosimone/utpintro/master/img/internals_cwnd.png

**Fig. 10** The flight_size and the congestion window (cwnd)
versus the elapsed time since the beginning of the emul_utp
experiment.

![emul_utp internals: timers][internals-timers]
[internals-timers]: https://raw.github.com/bassosimone/utpintro/master/img/internals_timers.png

**Fig. 11** The weighted average extra_delay and the smoothed rtt
(srtt) versus the elapsed time since the beginning of the emul_utp
experiment.

## 4. uTP

TODO
