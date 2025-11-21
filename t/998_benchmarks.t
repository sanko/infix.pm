use strict;
use warnings;
use lib './lib', '../lib', '../blib/arch/', 'blib/arch', '../', '.';
use Affix               qw[wrap affix libm Double direct_wrap direct_affix];
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
my $direct = direct_wrap( $libm, 'sin', '(double)->double' );
direct_affix( $libm, [ 'sin', 'direct_sin' ], '(double)->double' );

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
diag sprintf 'sin( %f ) = %f', $num, $sin;
subtest verify => sub {
    is direct_sin($num),  float( $sin, tolerance => 0.000001 ), 'direct affix correctly calculates sin';
    is $direct->($num),   float( $sin, tolerance => 0.000001 ), 'direct wrap correctly calculates sin';
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
        direct_affix => sub {
            my $x = 0;
            while ( $x < $depth ) { my $n = direct_sin($x); $x++; }
        },
        direct_wrap => sub {
            my $x = 0;
            while ( $x < $depth ) { my $n = $direct->($x); $x++; }
        },
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
    isnt fastest( -10, %benchmarks ), 'pure', 'The fastest method should not be pure Perl';
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

# WSL:
# running direct_wrap, direct_affix, plat_f, plat_a, pure, affix, wrap for 10 seconds each
# affix -  9 wallclock secs (10.58 usr +  0.00 sys = 10.58 CPU) @ 751291.68/s (n=7948666)
# direct_affix - 10 wallclock secs (10.53 usr +  0.00 sys = 10.53 CPU) @ 768814.72/s (n=8095619)
# direct_wrap - 10 wallclock secs (10.47 usr +  0.00 sys = 10.47 CPU) @ 737285.29/s (n=7719377)
# plat_a -  9 wallclock secs (10.40 usr +  0.00 sys = 10.40 CPU) @ 463780.48/s (n=4823317)
# plat_f - 10 wallclock secs (10.43 usr +  0.00 sys = 10.43 CPU) @ 101160.31/s (n=1055102)
#  pure -  8 wallclock secs (10.39 usr +  0.00 sys = 10.39 CPU) @ 1018920.02/s (n=10586579)
#  wrap - 11 wallclock secs (10.57 usr +  0.00 sys = 10.57 CPU) @ 737400.47/s (n=7794323)
#             Rate plat_f plat_a direct_wrap  wrap affix direct_affix  pure
# plat_f         101160/s    --  -78%  -86%  -86%  -87%  -87%  -90%
# plat_a         463780/s  358%    --  -37%  -37%  -38%  -40%  -54%
# direct_wrap    737285/s  629%   59%    --   -0%   -2%   -4%  -28%
# wrap           737400/s  629%   59%    0%    --   -2%   -4%  -28%
# affix          751292/s  643%   62%    2%    2%    --   -2%  -26%
# direct_affix   768815/s  660%   66%    4%    4%    2%    --  -25%
# pure          1018920/s  907%  120%   38%   38%   36%   33%    --
#
# Windows:
# running wrap, pure, plat_f, plat_a, direct_affix, direct_wrap, affix for 10 seconds each
#  affix - 11 wallclock secs (10.53 usr +  0.02 sys = 10.55 CPU) @ 654642.84/s (n=6904518)
# direct_affix - 10 wallclock secs (10.08 usr +  0.05 sys = 10.12 CPU) @ 697713.26/s (n=7063649)
# direct_wrap - 10 wallclock secs (10.51 usr +  0.03 sys = 10.55 CPU) @ 644699.89/s (n=6799005)
# plat_a - 10 wallclock secs (10.42 usr +  0.02 sys = 10.44 CPU) @ 218192.37/s (n=2277492)
# plat_f - 11 wallclock secs (10.45 usr +  0.00 sys = 10.45 CPU) @ 47983.35/s (n=501522)
#   pure - 11 wallclock secs (10.45 usr +  0.02 sys = 10.47 CPU) @ 527346.74/s (n=5520793)
#   wrap - 11 wallclock secs (10.38 usr +  0.03 sys = 10.41 CPU) @ 643539.11/s (n=6696668)
#             Rate plat_f plat_a  pure  wrap direct_wrap affix direct_affix
# plat_f         47983/s    --  -78%  -91%  -93%  -93%  -93%  -93%
# plat_a        218192/s  355%    --  -59%  -66%  -66%  -67%  -69%
# pure          527347/s  999%  142%    --  -18%  -18%  -19%  -24%
# wrap          643539/s 1241%  195%   22%    --   -0%   -2%   -8%
# direct_wrap   644700/s 1244%  195%   22%    0%    --   -2%   -8%
# affix         654643/s 1264%  200%   24%    2%    2%    --   -6%
# direct_affix  697713/s 1354%  220%   32%    8%    8%    7%    --
