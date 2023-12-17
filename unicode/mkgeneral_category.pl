#! /usr/bin/perl
#
# Compile UnicodeData.txt into C array declarations.
#
# The array's structure is [firstchar, lastchar, class], giving the
# linebreaking "class" for unicode character range firstchar-lastchar.
#
# The ranges are sorted in numerical order.
#
# An array gets generated for each block of 4096 unicode characters.
#
# Finally, two arrays get declared: a pointer to an array for each 4096
# unicode character block, and the number of elements in the array.
#
# The pointer is NULL for each block of 4096 unicode characters that is not
# defined in LineBreak.txt
#
# By definition, a unicode character that is not listed in the array is
# category Cn.

use strict;
use warnings;
use mkcommon;

my $obj=mkcommon->new;

open(UC, "<UnicodeData.txt") || die;

while (defined($_=<UC>))
{
    chomp;

    my @f=split(/;/);

    my $cp;

    eval "\$cp=0x$f[0]";

    $obj->range($cp, $cp, "UNICODE_GENERAL_CATEGORY_$f[2]");
}

$obj->output;
