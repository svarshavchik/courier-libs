#! /usr/bin/perl
#
# Creates a lookup table for canonical mappings in UnicodeData.txt

use strict;
use warnings;

open(F, "<UnicodeData.txt") || die;

my @mappings;
my @data;

while (defined($_=<F>))
{
    my @w=split(/;/, $_, -1);

    next unless $w[5];

    my $code=$w[0];

    my @mapping=split(/\s/, $w[5]);

    my $formatting_tag = "UNICODE_CANONICAL_FMT_NONE";

    if ($mapping[0] =~ /^</)
    {
	$formatting_tag = shift @mapping;

	$formatting_tag =~ s/<//g;
	$formatting_tag =~ s/>//g;
	$formatting_tag = "UNICODE_CANONICAL_FMT_" . uc($formatting_tag);
    };

    die "Too long\n" if (scalar @mapping) > 0xFFFF;

    my $dec_code;

    eval "\$dec_code=0x$code\n";

    push @data, [$dec_code, "\t{0x$code, (unsigned char)$formatting_tag, "
	. (scalar @mapping) . ", "
	. scalar(@mappings) . "}" ];
    push @mappings, @mapping;
}

my $hash_size = int( (scalar @data) * 3 / 4);

my %buckets;

my $keep_going = 1;

while ($keep_going)
{
    %buckets = ();

    $keep_going = 0;

    foreach my $m (@data)
    {
	my $bucket = $m->[0] % $hash_size;

	push @{$buckets{$bucket}}, $m;

	if ((scalar @{$buckets{$bucket}}) > 3)
	{
	    $keep_going = 1;
	    ++$hash_size;
	    last;
	}
    }
}

print "#define HASH_SIZE $hash_size\n";

@data = ();

my $pfix = "";

print "static const unsigned short canon_map_hash[]={\n";

foreach my $bucket (0.. ($hash_size)-1)
{
    print "$pfix\t" . (scalar @data);
    $pfix = ",\n";

    push @data, @{ $buckets{$bucket} // [] };
}


print "};\n\nstatic const struct canon_map_table canon_map_lookup[]={\n";

$pfix = "";

foreach my $m (@data)
{
    print "$pfix" . $m->[1];
    $pfix = ",\n";
}

print "\n};\n\nstatic const char32_t canon_map_values[]={\n";

$pfix="";

foreach my $v (@mappings)
{
    print "$pfix\t0x$v";

    $pfix=",\n";
}
print "};\n";
