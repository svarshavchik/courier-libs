#! /usr/bin/perl
#
# Compile Scripts.txt into C array declarations.
#
# scripts: an array of script names. The last entry will be for "Unknown";
#
# unicode_rangetab:
#
# The array's structure is [firstchar, lastchar], listing unicode character
# range with the same script. firstchar and lastchar is the last byte in the
# character range/
#
# The ranges are sorted in numerical order.
#
# unicode_classtab:
#
# An array of the same size as unicode_rangetab, gives the index of the
# unicode range's script name, in the scripts array. Neither rangetab nor
# classtab will have entries pointing to "Unknown". All unicode characters
# not in rangetab default to "Unknown";
#
# unicode_indextab:
#
# For each group of 256 characters, an index into rangetab/classtab where
# ranges for those groups of 256 characters are start.
#
# unicode_rangetab stores only the low byte of the starting/ending character
# number.

use strict;
use warnings;
use mkcommon;

my $obj=mkcommon->new;

$obj->{proptype}="char *";

open(F, "<Scripts.txt") || die;

my @table;

my %scriptnames;

my $counter=0;

while (defined($_=<F>))
{
    chomp;

    next unless /^([0-9A-F]+)(\.\.([0-9A-F]+))?\s*\;\s*([^\s]+)\s*/;

    my $f=$1;
    my $l=$3;
    my $s=$4;

    $l=$f unless $l;

    eval "\$f=0x$f";
    eval "\$l=0x$l";

    $scriptnames{$s} //= ++$counter;

    push @table, [$f, $l, $scriptnames{$s} . "-1"];
}

print "static const char * const scripts[]={\n";

foreach (sort { $scriptnames{$a} <=> $scriptnames{$b}} keys %scriptnames)
{
    print "\t\"$_\",\n";
}

print "\t\"Unknown\"};\n";

grep {

    $obj->range($$_[0], $$_[1], $$_[2]);

} sort { $$a[0] <=> $$b[0] } @table;

$obj->output;
