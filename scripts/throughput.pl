#!/usr/bin/env perl -w

#
# Copyright (c) 2013
#     Simone Basso, Nexa Center for Internet & Society,
#     Politecnico di Torino (DAUIN).
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

#
# Measure the throughput at the sender (which is running MacOS)
#

$|++;

use strict;
use warnings;

use Time::HiRes qw( gettimeofday usleep );

my $INTERFACE = "en1";
my $SUBNET = "192.168.0";

my $prev_cnt = 0;
my $prev_usec = 0;
my $prev_sec = 0;
while () {
    my ($sec, $usec) = gettimeofday();
    my @info = `netstat -bi -I $INTERFACE`;
    foreach my $line (@info) {

        my @tokens = split(" ", $line);
        if ($tokens[3] =~ $SUBNET) {
            my $cnt = $tokens[9];
            if ($prev_sec > 0) {
                print $sec + $usec / 1000000.0, " ",
                  ($cnt - $prev_cnt) / ($sec + $usec / 1000000.0
                    - $prev_sec - $prev_usec / 1000000.0),
                  "\n";
            }
            $prev_sec = $sec;
            $prev_usec = $usec;
            $prev_cnt = $cnt;
        }

        usleep(250000);
    }
}
