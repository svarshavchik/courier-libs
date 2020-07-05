#! /usr/bin/perl

use strict;
use warnings;

while (<>)
{
    last if m@^/\* BIDI_CLASS_LIST@;
}

while (<>)
{
    last if m@^}@;
    next if /\{/;
    next if /^\s*$/;
    next if m@/\*@;

    die unless /UNICODE_BIDI_CLASS_(.*),/;

    print "{\"$1\", UNICODE_BIDI_CLASS_$1},\n";
}
