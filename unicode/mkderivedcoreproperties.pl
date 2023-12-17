#! /usr/bin/perl
#
# Compile DerivedCoreProperties.txt into C array declarations.

use strict;
use warnings;
use mkcommon;

open(F, "<DerivedCoreProperties.txt") || die;

my %flagobjs;
my %propobjs;
my %propvalues;
my %sorted_prop_values;

my $obj = mkcommon->new;

my %first;

while (defined($_=<F>))
{
    chomp;

    next unless /^
		( [0-9A-F]+ )
		(\.\.( [0-9A-F]+ ))?
		\s*\;\s*
		([^\s;#]+)\s*
                (;\s*( [^\s;#]+))?
		/xx;

    my $f=$1;
    my $l=$3;
    my $prop=$4;
    my $val=$6;

    $l=$f unless $l;

    eval "\$f=0x$f";
    eval "\$l=0x$l";

    if ($val)
    {
	$propobjs{$prop} //=
	    mkcommon->new(
		prefix => $prop,
	    );

	$propvalues{$prop}{$val}=1;
	my $n=uc("UNICODE_DERIVED_${prop}_${val}");

	push @{$sorted_prop_values{$prop}}, [$f, $l, $n];
	$first{$prop}{$n} //= $f;
	next;
    }

    my $obj = $flagobjs{$prop} //=
	mkcommon->new(
	    prefix => $prop,
	    noclass => 1,
	);

    $obj->range($f, $l, 1);
}

foreach my $prop (sort keys %sorted_prop_values)
{
    my $obj = $propobjs{$prop};

    foreach my $rec (sort { $a->[0] <=> $b->[0] }
	@{$sorted_prop_values{$prop}})
    {
	my ($f, $l, $n) = @$rec;

	$obj->range($f, $l, $n);
    }
}

my $bit;

print "/*\n";

foreach my $prop (sort keys %propvalues)
{
    my $bit = 1;
    my $values = $propvalues{$prop};

    my $mask = 1;
    my $n_values = 2;
    my $shifted_mask = $bit;
    my $first_value = $bit;
    my $next_value = $first_value;

    while ($n_values <= scalar @{[ keys %$values ]})
    {
	$bit <<= 1;
	$n_values <<= 1;

	$shifted_mask = ($shifted_mask << 1) | $shifted_mask;
    }

    print sprintf("#define %-50s 0x%08x\n",
	uc("UNICODE_DERIVED_${prop}_MASK"), $shifted_mask);

    foreach my $val (sort keys %$values)
    {
	print sprintf("#define %-50s 0x%08x\n",
	    uc("UNICODE_DERIVED_${prop}_${val}"), $next_value);
	$next_value = $next_value + $first_value;
    }
}
print "*/\n";

foreach my $obj (sort keys %propobjs)
{
    $propobjs{$obj}->output;
}

foreach my $obj (sort keys %flagobjs)
{
    $flagobjs{$obj}->output;

}

foreach my $obj (sort keys %propobjs)
{
    $propobjs{$obj}->lookup_call(
	classtype => 'int',
	name      => "unicode_derived_" . lc($obj) . "_lookup",
    );
}

foreach my $obj (sort keys %flagobjs)
{
    $flagobjs{$obj}->lookup_call(
	classtype => 'int',
	name      => "unicode_derived_" . lc($obj) . "_lookup",
    );
}
