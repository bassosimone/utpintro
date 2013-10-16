# Makefile

#
# Copyright (c) 2013
#     Nexa Center for Internet & Society, Politecnico di Torino (DAUIN)
#     and Simone Basso <bassosimone@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject
# to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
# ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
# CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

.PHONY: all clean img

CFLAGS = -Wall -I libevent/include -I libutp

LIBEVENT = libevent/.libs/libevent_core.a
LIBUTP = libutp/libutp.a

all: tcptest utptest utptest_emul

clean:
	rm -rf -- `cat .gitignore`
	rm -rf -- libevent/test-driver
	(cd libevent && make distclean) || true
	(cd libutp && make clean) || true

img:
	( \
	 cd img && \
	 for FILE in *.dia; do \
	     STEM=$$(echo $$FILE|sed 's/.dia$$//g'); \
	     dia -e $$STEM.svg $$FILE; \
	 done && \
	 for FILE in *.gpl; do \
	     gnuplot $$FILE; \
	 done && \
	 for FILE in *.svg; do \
	     STEM=$$(echo $$FILE|sed 's/.svg$$//g'); \
	     inkscape -d 128 -e $$STEM.png $$STEM.svg; \
	 done \
	)

$(LIBEVENT):
	( \
	 cd libevent && \
	 autoreconf -i && automake && \
         ./configure --disable-thread-support \
	             --disable-openssl \
	             --disable-libevent-install \
	             --enable-shared=no && \
	 make \
	)

$(LIBUTP):
	(cd libutp && make)

emul_utp.o: emul_utp.c
strtonum.o: strtonum.c strtonum.h
tcptest.o: $(LIBEVENT) tcptest.c strtonum.h
utptest.o: $(LIBEVENT) utptest.c strtonum.h

#
# On Linux systems with glibc < 2.17 you need to link with librt, otherwise
# the link stage fails. According to my limited tests you don't need to
# explicitly link with librt under Slackware -current (post 14.0) but you
# need to do so with Ubuntu 12.04.
#
# To explicitly link with librt, run `make LIBRT=-lrt` instead of
# just running `make'.
#
LIBRT =

tcptest: strtonum.o tcptest.o $(LIBEVENT)
	$(CC) -o tcptest strtonum.o tcptest.o $(LIBEVENT) $(LIBRT)

utptest: strtonum.o utptest.o $(LIBEVENT) $(LIBUTP)
	$(CC) -o utptest strtonum.o utptest.o $(LIBEVENT) $(LIBUTP) $(LIBRT)

utptest_emul: emul_utp.o strtonum.o utptest.o $(LIBEVENT)
	$(CC) -o utptest_emul emul_utp.o strtonum.o utptest.o \
	    $(LIBEVENT) $(LIBRT)
