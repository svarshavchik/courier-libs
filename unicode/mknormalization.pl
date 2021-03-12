#! /usr/bin/perl
#
# Compile DerivedNormalizationProps.txt into C array declarations.
#
# See mkgraphemebreak.pl for the description of the arrays' structures.

use strict;
use warnings;
use mkcommon;

open(F, "<DerivedNormalizationProps.txt") || die;

my @full_composition_exclusion;

my $nfc = mkcommon->new(prefix => 'nfc_qc');
my $nfkc = mkcommon->new(prefix => 'nfkc_qc');

while (defined($_=<F>))
{
    chomp;

    next unless /^
    	 	( [0-9A-F]+ )
		(\.\.( [0-9A-F]+ ))?
		\s*\;\s*
		([^\s;]+)\s*
		(;\s*([^\s;]+))?
		/xx;

    my $f=$1;
    my $l=$3;
    my $t=$4;
    my $v=$6;

    $l=$f unless $l;

    eval "\$f=0x$f";
    eval "\$l=0x$l";

    if ($t eq 'Full_Composition_Exclusion')
    {
	push @full_composition_exclusion, [$f, $l];
    }

    if ($t eq 'NFC_QC')
    {
	$nfc->range($f, $l, "UNICODE_NFC_QC_$v");
    }

    if ($t eq 'NFKC_QC')
    {
	$nfkc->range($f, $l, "UNICODE_NFKC_QC_$v");
    }
}

my $obj=mkcommon->new(prefix => 'exclusion', noclass => 1);

foreach (sort { $$a[0] <=> $$b[0] } @full_composition_exclusion)
{
    $obj->range($$_[0], $$_[1]);
}

print "/* Full_Composition_Exclusion table lookup */\n";
print "#ifdef exclusion_table\n";

$obj->output;

print "#endif\n";

print "\n/* NFC_QC table lookup */\n";

$nfc->output;

print "\n/* NFC_KQC table lookup */\n";

$nfkc->output;

$obj = mkcommon->new(prefix => 'ccc');

# Construct decomposition mappings.
#
# We use unicode_tab to look up ccc values.
#
# Along the way we collect all decomposition mappings into %decomps

open(F, "<UnicodeData.txt") || die;

my %decomps;

# All the decomposed values will be written into an array, so we'll record
# the starting of each mapping in the array.
my $decomp_index = 0;

# We will also number each decomposition mapping.
my $ordinal = 0;

print "\n/* Linear dump of decomposed strings */\n";

print "static const char32_t decompositions[]={\n";
my $comma = "\t";

while(defined($_=<F>))
{
    s/\#.*//;

    my @w=split(/\;/);

    s/^\s+// for @w;
    s/\s+$// for @w;

    my $n;

    eval "\$n=0x$w[0]";

    die $@ if $@;

    $obj->range($n, $n, $w[3]) if $w[3] ne "0";

    if ($w[5] ne "")
    {
	my @v = split(/\s+/, $w[5]);

	my $type = $v[0];

	if ($type =~ /^\</)
	{
	    shift @v;
	    $type =~ s/<//;
	    $type =~ s/>//;
	    $type = "UNICODE_CANONICAL_FMT_" . uc($type);
	}
	else
	{
	    $type = "UNICODE_CANONICAL_FMT_NONE";
	}

	@v = map {
	    my $dec;

	    eval "\$dec = 0x$_";
	    die $@ if $@;

	    $dec;
	} @v;

	$decomps{$n} = {
	    char  => $n,
	    index => $decomp_index,
	    type   => $type,
	    ordinal => $ordinal++,
	    values => \@v,
	};

	foreach my $hex (@v)
	{
	    print "${comma}0x" .sprintf("%04x", $hex);

	    $comma=", ";

	    if ( ((++$decomp_index) % 8) == 0)
	    {
		$comma=",\n\t";
	    }
	}
    }
}

print "};\n\n";

# Figure out the optimal hash bucket size.

my $hash = scalar @{ [ keys %decomps ] };
my $maxbuckets = 3; # Got lucky

foreach (0..100)
{
    my %buckets;

    my $good = 1;

    foreach my $ch (sort { $a <=> $b } keys %decomps)
    {
	my $bucket = ($buckets{$ch % $hash} //= []);

	push @$bucket, $decomps{$ch};

	if (scalar @$bucket > $maxbuckets)
	{
	    $good = 0;
	    last;
	}
    }

    unless ($good)
    {
	++$hash;
	next;
    }

    print qq(
/*
** Hash table lookup of decompositions, hashed by unicode char % $hash with
** $maxbuckets slots in each bucket for unicode chars with the same hash value.
*/

struct decomposition_info {
    char32_t ch;                 /* Unicode character, 0: unused slot */
    uint16_t decomp_index;       /* Index into the decompositions array */
    uint8_t decomp_size;         /* Number of chars in the decomposition */
    uint8_t decomp_type;         /* Type of decomposition */
};\n);

    print "\nstatic const struct decomposition_info decomp_lookup[][$maxbuckets]={\n";

    $comma="\t";

    foreach my $bucket ( 0..($hash-1) )
    {
	print "${comma}{";
	$comma = "";

	my $info = $buckets{$bucket};

	foreach my $bucket_index (1..$maxbuckets)
	{
	    my $info1 = shift @$info;

	    print "${comma}{";

	    unless ($info1)
	    {
		print "0, 0, 0, 0";
	    }
	    else
	    {
		print join(", ",
		    "0x" . sprintf("%04x", $info1->{char}),
		    $info1->{index},
		    scalar @{ $info1->{values} },
		    $info1->{type});

		$info1->{bucket} = [$bucket, $bucket_index-1];
	    }
	    print "}";
	    $comma=", ";
	}
	print "}";
	$comma = ",\n\t";
    }
    print "\n};\n";

    print "\n/* CCC table lookup */\n";

    $obj->output;

    my @all_decomps = map {
	die "What's this?\n" if scalar @{$_->{values}} != 2;
	$_;
    } grep {
	my $ch = $_->{char};

	my $found = grep { $ch >= $_->[0] && $ch <= $_->[1] }
	@full_composition_exclusion;

	!$found && $_->{type} eq "UNICODE_CANONICAL_FMT_NONE";

    } values %decomps;

    $maxbuckets = 4;

    foreach my $mult1 (41..71)
    {
	foreach my $mult2 (41..71)
	{
	    $hash = scalar @all_decomps;

	    foreach (0..500)
	    {
		my %buckets;
		my $good;

		%buckets = ();

		$good = 1;

		foreach my $ch (@all_decomps)
		{
		    my ($ch1, $ch2) = @{$ch->{values}};

		    my $bucket = ($ch1 * $mult1 + $ch2 * $mult2 ) % $hash;

		    push @{$buckets{$bucket} //= []}, $ch;

		    if (scalar @{$buckets{$bucket}} > $maxbuckets)
		    {
			$good = 0;
			last;
		    }
		}

		if ($good)
		{
		    print qq(
/*
** Hashed canonical compositions: first char, last char, composition.
*/

	    static const char32_t canonical_compositions[][3]={);

		    $comma="\n\t";

		    my $counter=0;

		    foreach (0..($hash-1))
		    {
			foreach my $bucket (@{ $buckets{$_} // []})
			{
			    print $comma . "{"
				. join(",",
				    map { sprintf("0x%04x", $_); }
				    $bucket->{values}[0],
				    $bucket->{values}[1],
				    $bucket->{char}) . "}";

			    $comma=", ";
			    $comma=",\n\t" if (++$counter % 8) == 0;
			}
		    }
		    print qq(
};

/*
** Canonical composition lookup: first*canonical_mult1+second*canonical_mult2
** modulo the size of the canonical_compositions_lookup table.
**
** The value in the table is the index into canonical_composition table.
*/

#define canonical_mult1 $mult1
#define canonical_mult2 $mult2

#ifndef exclusion_table

static uint16_t canonical_compositions_lookup[]={
);
		    $comma="\t";

		    $counter=0;

		    foreach my $index (0..($hash-1))
		    {
			print $comma . $counter;
			$comma=",\n\t";

			$counter += scalar @{ $buckets{$index} // []};
		    }
		    print "\n};\n";
		    print "#endif\n";
		    exit(0);
		}
		++$hash;
	    }
	}
    }
    die "Hashing of composition failed.\n";
}

die "Hashing of decompositions failed.\n";
