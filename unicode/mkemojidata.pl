#! /usr/bin/perl
#
# Compile emoji-data.txt into C array declarations.
#
# The arrays' structure is [firstchar, lastchar], listing the emojis with the
# given property.

use strict;
use warnings;

open(F, "<emoji-data.txt") || die;

my $curclass;
my $lastl;

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

    if ((! defined $curclass) || $t ne $curclass)
    {
	if (defined $curclass)
	{
	    print "};\n\n";
	}

	$curclass = $t;

	print "static const char32_t unicode_emoji_" . lc($curclass)
	    . "_lookup[][2]={\n";
    }
    else
    {
	die "Not sorted\n" unless $l > $lastl;
    }
    print "\t{$f, $l},\n";

    $lastl=$l;
}
print "};\n\n";
