#! /usr/bin/perl
#
# Compile Categories.txt into C array declarations.
#
# The array's structure is [firstchar, lastchar, mask], giving the
# category of the character range firstchar-lastchar.
#
# The ranges are sorted in numerical order.
#
# An array gets generated for each block of 4096 unicode characters.
#
# Finally, two arrays get declared: a pointer to an array for each 4096
# unicode character block, and the number of elements in the array.
#
# The pointer is NULL for each block of 4096 unicode characters that is not
# defined in Categories.txt

use strict;
use warnings;
use mkcommon;

my %categories;

my $obj=mkcommon->new(classtype => "uint32_t");

open(F, "Categories.txt") || die;

while (defined($_=<F>))
{
    chomp;

    my @w=split(/\t/);

    my $f = $w[0];

    my @combined_category;

    my $categories = \%categories;

    foreach my $i (2..5)
    {
	my $c = uc($w[$i]);

	last unless $c;

	$c =~ s/[;\-\s]+/_/g;

	my $n = $i-1;

	$c = "UNICODE_CATEGORY_${n}_$c";

	$categories = ($categories->{$c} //= {});

	push @combined_category, $c;
    }

    eval "\$f=0x$f";

    $obj->range($f, $f, join("|",@combined_category));
}

open(H, ">courier-unicode-categories-tab.h.tmp") or die;

my @counters = (0, 0, 0, 0);
my %seen;

sub print_categories {
    my ($categories, $level) = @_;

    my @names = sort keys %$categories;

    foreach my $name (@names)
    {
	my $seen = 1;
	my $v = $seen{$name};

	unless ($v)
	{
	    $seen = 0;

	    die if ($v = ++$counters[$level]) > 255;
	    $seen{$name} = $v;
	}

	my $s = "";

	$s .= "/* "
	    if $seen;

	$s .= "#define "
	    . ("    " x $level)
	    . $name ;

	$s = substr($s . (" " x 66), 0, 66)
	    if length($s) < 66;

	print H "$s 0x"
	    . ("00" x $level) . sprintf("%02x", $v)
	    . ("00" x (3-$level));

	print H " */"
	    if $seen;
	print H "\n";

	print_categories($categories->{$name}, $level+1);
    }
}

print_categories(\%categories, 0);
$obj->output;
close(H) or die;
rename("courier-unicode-categories-tab.h.tmp",
    "courier-unicode-categories-tab.h") or die;
