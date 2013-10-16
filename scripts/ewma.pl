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
# Computes the exponentially-weighted moving average (EWMA).
#
# See <http://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average>.
#

use strict;
use warnings;

my $lambda = 0.25;
my $ewma = -1;

while (<>) {
    if (/^#/) {
        print;
        next;
    }
    my @tokens = split(" ", $_);
    if (!@tokens) {
        print "\n";
        next;
    }
    if ($ewma < 0) {
        $ewma = $tokens[1];
        print "$ewma\n";
        next;
    }
    $ewma = $lambda * $tokens[1] + (1 - $lambda) * $ewma;
    print "$tokens[0] $ewma\n";
}
