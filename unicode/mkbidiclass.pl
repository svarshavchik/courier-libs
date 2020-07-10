#! /usr/bin/perl
#
# Compile DerivedBidiClass.txt into C array declarations.
#
# The array's structure is [firstchar, lastchar, class], giving the
# Bidi Class for unicode character range firstchar-lastchar.
#
# The ranges are sorted in numerical order.
#
# An array gets generated for each block of 4096 unicode characters.
#
# Finally, two arrays get declared: a pointer to an array for each 4096
# unicode character block, and the number of elements in the array.

use strict;
use warnings;
use mkcommon;

my $obj=mkcommon->new;

open(F, "<DerivedBidiClass.txt") || die;

my @table;

while (defined($_=<F>))
{
    chomp;

    next unless /^([0-9A-F]+)(\.\.([0-9A-F]+))?\s*\;\s*([^\s#]+)\s*/;

    my $f=$1;
    my $l=$3;
    my $t=$4;

    $l=$f unless $l;

    eval "\$f=0x$f";
    eval "\$l=0x$l";

    push @table, [$f, $l, $t];
}

#my $prevl = -1;

grep {
    #if ($prevl + 1 < $$_[0])
    #{
    #	$obj->range($prevl+1, $$_[0]-1, "UNICODE_BIDI_TYPE_$$_[2]");
    #}
    #$prevl = $$_[1];
    $obj->range($$_[0], $$_[1], "UNICODE_BIDI_TYPE_$$_[2]");
} sort { $$a[0] <=> $$b[0] } @table;

$obj->output;
