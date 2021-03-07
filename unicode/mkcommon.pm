package mkcommon;

use 5.012002;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

# Common code used to create Unicode lookup tables.
#
# Generates C code that declares a bunch of arrays.
#
# The 'rangetab' array's structure is [firstchar, lastchar], and the
# 'classtab' array has the same size containing "class", giving the
# associated "value" for unicode character range firstchar-lastchar.
#
# The ranges are sorted in numerical order, but rangetab stores the least
# singificant byte of the 32-bit Unicode character (firstchar and lastchar).
# The leading bytes of both firstchar and lastchar are the same.
#
# In this manner, the Unicode data gets divided into 256 character blocks.
#
# The "starting_indextab" array enumerates which 256 character blocks have
# any data in the "rangetab" array. 256 characters that don't wind up
# with any data get skipped entirely.
#
# The "starting_pagetab" array is the starting index in the "rangetab"
# array for the corresponding 256 character block. "starting_indextab" and
# "starting_pagetab" arrays have the same size.
#
# "starting_indextab" is sorted, a binary search finds the start of the
# 256 character block containing the character, via "starting_pagetab".
#
# The end of the 256 character block in "rangetab" is given by the
# starting index of the next 256 character block, or the end of the "rangetab"
# array.
#
# A binary search is done to locate the range containing the given character,
# and the associated value from "classtab" gets returned.

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# This allows declaration	use mkcommon ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(

) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(

);

our $VERSION = '0.01';

my $BLOCK_SIZE=256;

# Preloaded methods go here.

sub new {

    my ($this, %params) = @_;

    my $class = ref($this) || $this;
    my $self = \%params;

    bless $self, $class;

    $self->{'char_array'}=[];
    $self->{'char_class'}=[];
    $self->{'char_start'}=[0];

    $self->{'last_block'}=-1;
    $self->{'last'}="";
    $self->{'last_f'}=0;
    $self->{'last_l'}=0;

    $self->{"classtype"} //= "uint8_t";
    $self->{"prefix"} //= "unicode";

    return $self;
}

sub _doemit_block {
    my $this=shift;

    my $f=shift;
    my $l=shift;

    push @{$this->{'char_array'}}, [$f, $l];
    push @{$this->{'char_class'}}, $this->{'last'};
}

sub _doemit_endblock {

    my $this=shift;

    push @{$this->{'char_start'}}, $#{$this->{'char_array'}}+1;
}

# _doemit invokes _doemit_block() for each unicode char range with a given
# linebreaking class. However, once a unicode char range starts in a different
# $BLOCK_SIZE character class, call _doemit_endblock() before calling _doemit_block().
#
# If a single unicode char range crosses a $BLOCK_SIZE character class boundary,
# split it at the boundary; call _doemit_endblock() to finish the current $BLOCK_SIZE
# char boundary, call _doemit_endblock(), then call _doemit_block() for the
# rest of the char range.


sub _doemit {

    my $this=shift;

    $this->_doemit_endblock()
	if int($this->{'last_f'} / $BLOCK_SIZE)
	!= $this->{'last_block'} && $this->{'last_block'} != -1;

    if (int($this->{'last_f'} / $BLOCK_SIZE) != int($this->{'last_l'} / $BLOCK_SIZE))
    {
	while (int($this->{'last_f'} / $BLOCK_SIZE) != int($this->{'last_l'} / $BLOCK_SIZE))
	{
	    my $n=int($this->{'last_f'} / $BLOCK_SIZE) * $BLOCK_SIZE + ($BLOCK_SIZE-1);

	    $this->_doemit_block($this->{'last_f'}, $n);
	    $this->_doemit_endblock();
	    $this->{'last_f'}=$n+1;
	}
    }
    $this->_doemit_block($this->{'last_f'}, $this->{'last_l'});

    $this->{'last_block'}=int($this->{'last_l'} / $BLOCK_SIZE);
}

#
# Coalesce adjacent unicode char blocks that have the same linebreaking
# property. Invoke _doemit() for the accumulate unicode char range once
# a range with a different linebreaking class is seen.

sub range {

    my $this=shift;

    my $f=shift;
    my $l=shift;
    my $t=shift // 'NONE';

    if ($this->{'last_l'} + 1 == $f && $this->{'last'} eq $t)
    {
	$this->{'last_l'}=$l;
	return;
    }

    $this->_doemit() if $this->{'last'};  # New linebreaking class

    $this->{'last_f'}=$f;
    $this->{'last_l'}=$l;
    $this->{'last'}=$t;
}

sub output {
    my $this=shift;

    $this->_doemit();  # Emit last linebreaking unicode char range class

    my $prefix = $this->{"prefix"};

    print "static const uint8_t ${prefix}_rangetab[][2]={\n";

    my $comma="\t";

    my $modulo=sprintf("0x%X", $BLOCK_SIZE-1);

    foreach ( @{$this->{'char_array'}} )
    {
	print "${comma}{0x" . sprintf("%04x", $$_[0]) . " & $modulo, 0x"
	    . sprintf("%04x", $$_[1]) . " & $modulo}";
	$comma=",\n\t";
    }

    print "};\n\n";

    unless ($this->{noclass})
    {
	print "static const " . $this->{classtype}
	. " ${prefix}_classtab[]={\n";

	$comma="\t";
	foreach ( @{$this->{'char_class'}} )
	{
	    print "${comma}$_";
	    $comma=",\n\t";
	}

	print "};\n\n";
    }

    my $prev_block=-1;

    my @starting_indextab;
    my @starting_pagetab;

    foreach my $sp (@{$this->{'char_start'}})
    {
	my $block=int($this->{'char_array'}->[$sp]->[0] / $BLOCK_SIZE);

	if ($block != $prev_block)
	{
	    push @starting_indextab, $block;
	    push @starting_pagetab, $sp;
	    $prev_block=$block;
	}
    }

    print "static const size_t ${prefix}_starting_indextab[]={\n";

    $comma="\t";

    foreach (@starting_indextab)
    {
	print "$comma$_";
	$comma=",\n\t";
    }

    print "\n};\n\nstatic const char32_t ${prefix}_starting_pagetab[]={\n";

    $comma="\t";

    foreach (@starting_pagetab)
    {
	my $sp=$_;

	print "$comma$sp";
	$comma=",\n\t";
    }

    print "\n};\n\n";
}

1;
