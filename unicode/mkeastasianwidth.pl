#! /usr/bin/perl

# USAGE: perl mkeastasianwidth.pl > charwidth.c

use IO::File;
use strict;
use warnings;

foreach my $pass (

    {
	header => "char32_t unicode_eastasia_tab[][2]",
	emit => sub {
	    my ($pb, $pe, $pv) = @_;

	    printf ("{0x%04x, 0x%04x},\n", $pb, $pe);
	},
    },
    {
	header => "unicode_eastasia_t unicode_eastasia_v[]",
	emit => sub {
	    my ($pb, $pe, $pv) = @_;

	    printf ("UNICODE_EASTASIA_%s,\n", $pv);
	},
    })
{
    my $emit = $pass->{emit};


    my $fh=new IO::File "<EastAsianWidth.txt";

    my $pb=-1;
    my $pe=-1;
    my $pv="";

    print "static const " . $pass->{header} . "={\n";

    my $full = sub {
	my $b=hex(shift);
	my $e=hex(shift);
	my $v=shift;

	if ($b == $pe+1 && $v eq $pv)
	{
	    $pe=$e;
	    return;
	}

	$emit->($pb, $pe, $pv) unless $pb < 0;

	$pb=$b;
	$pe=$e;
	$pv=$v;
    };


    while (defined($_=<$fh>))
    {
	chomp;
	s/#.*//;

	my @w=split(/;/);

	next unless @w;
	grep {s/^\s*//; s/\s*$//; } @w;

	if ($w[0] =~ /(.*)\.\.(.*)/)
	{
	    $full->($1, $2, $w[1]);
	}
	else
	{
	    $full->($w[0], $w[0], $w[1]);
	}
    }

    $emit->($pb, $pe, $pv);
    print "};\n\n";
}
