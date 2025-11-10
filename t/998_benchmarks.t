use strict;
use warnings;
use lib './lib', '../lib', '../blib/arch/', 'blib/arch', '../', '.';
use Affix               qw[wrap affix libm Double];
use Test2::Tools::Affix qw[:all];
use Config;

# use Test2::Require::AuthorTesting;
use Benchmark qw[:all];
$|++;

# Conditionally load FFI::Platypus
my $has_platypus;

BEGIN {
    eval 'use FFI::Platypus';
    unless ($@) {
        $has_platypus = 1;
        diag 'FFI::Platypus found, including it in benchmarks.';
    }
    else {
        diag 'FFI::Platypus not found, skipping its benchmarks.';
    }
}
my $libm = '' . libm();
diag 'libm: ' . $libm;

# FFI Setup
# Affix / Wrap setup
my $wrap_sin = wrap( $libm, 'sin', '(double)->double' );
affix( $libm, [ sin => 'affix_sin' ], '(double)->double' );

# FFI::Platypus setup (only if available)
my $platypus_sin;
if ($has_platypus) {
    my $ffi = FFI::Platypus->new( api => 2, lib => $libm );

    # Use find_lib with a named argument
    $ffi->attach( [ sin => 'platypus_sin' ], ['double'] => 'double' );
    $platypus_sin = $ffi->function( 'sin', ['double'] => 'double' );
}

# Verification
my $num = rand(time);
my $sin = sin $num;
subtest verify => sub {
    is $wrap_sin->($num), float( $sin, tolerance => 0.000001 ), 'wrap correctly calculates sin';
    is affix_sin($num),   float( $sin, tolerance => 0.000001 ), 'affix correctly calculates sin';
    is sin($num),         float( $sin, tolerance => 0.000001 ), 'pure perl correctly calculates sin';

    # Conditionally run Platypus verification
    if ($has_platypus) {
        is $platypus_sin->($num), float( $sin, tolerance => 0.000001 ), 'platypus [function] correctly calculates sin';
        is platypus_sin($num),    float( $sin, tolerance => 0.000001 ), 'platypus [attach] correctly calculates sin';
    }
};

# Benchmarks
my $depth = 20;
subtest benchmarks => sub {
    my $todo       = todo 'these are fun but not important; we will not be beating opcodes';
    my %benchmarks = (
        pure => sub {
            my $x = 0;
            while ( $x < $depth ) { my $n = sin($x); $x++ }
        },
        wrap => sub {
            my $x = 0;
            while ( $x < $depth ) { my $n = $wrap_sin->($x); $x++ }
        },
        affix => sub {
            my $x = 0;
            while ( $x < $depth ) { my $n = affix_sin($x); $x++ }
        }, (
            $has_platypus ?

                # Conditionally add Platypus benchmark
                (
                plat_f => sub {
                    my $x = 0;
                    while ( $x < $depth ) { my $n = $platypus_sin->($x); $x++ }
                },
                plat_a => sub {
                    my $x = 0;
                    while ( $x < $depth ) { my $n = platypus_sin($x); $x++ }
                }
                ) :
                ()
        )
    );
    isnt fastest( -5, %benchmarks ), 'pure', 'The fastest method should not be pure Perl';
};

# Helper Function
# Cribbed from Test::Benchmark
sub fastest {
    my ( $times, %marks ) = @_;
    diag sprintf 'running %s for %s seconds each', join( ', ', keys %marks ), abs($times);
    my @marks;
    my $len = [ map { length $_ } keys %marks ]->[-1];
    for my $name ( sort keys %marks ) {
        my $res = timethis( $times, $marks{$name}, '', 'none' );
        my ( $r, $pu, $ps, $cu, $cs, $n ) = @$res;
        push @marks, { name => $name, res => $res, n => $n, s => ( $pu + $ps ) };
        diag sprintf '%' . ( $len + 1 ) . 's - %s', $name, timestr($res);
    }
    my $results = cmpthese {
        map { $_->{name} => $_->{res} } @marks
    }, 'none';
    my $len_1 = [ map { length $_->[1] } @$results ]->[-1];
    diag sprintf '%-' . ( $len + 1 ) . 's %' . ( $len_1 + 1 ) . 's' . ( ' %5s' x scalar keys %marks ), @$_ for @$results;
    [ sort { $b->{n} * $a->{s} <=> $a->{n} * $b->{s} } @marks ]->[0]->{name};
}
done_testing;
