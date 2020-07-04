#! /usr/bin/perl
#
# Compile BidiBrackets.txt or BidiMirroring.txt into C array declarations.
#
# The array's structure is [firstchar, lastchar, property (BidiBrackets)]

use strict;
use warnings;

open(F, "<" . shift) || die;

my $extra = shift;

my $lastv;

while (defined($_=<F>))
{
    chomp;

    s/#.*//;

    my @w = split(/;/);

    s/^\s+// for @w;

    s/\s+$// for @w;

    next unless (scalar @w) >= 2;

    my ($code_point, $other_code_point, $other) = @w;

    eval "\$code_point=0x$code_point";
    eval "\$other_code_point=0x$other_code_point";

    die "Not sorted\n" if (defined $lastv) && $lastv >= $code_point;

    $lastv = $code_point;

    if ($extra)
    {
	print "UNICODE_BIDI_$other,\n";
    }
    else
    {
	print "{$code_point, $other_code_point},\n";
    }
}
