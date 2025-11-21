use strict;
use warnings;
use lib './lib', '../lib', '../blib/arch/', 'blib/arch', '../', '.';
use Affix               qw[wrap affix libm Double affix_bundle];
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
my $direct = affix_bundle( $libm, 'sin', '(double)->double' );

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
    is $direct->($num),   float( $sin, tolerance => 0.000001 ), 'direct correctly calculates sin';
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
        direct => sub {
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
# running plat_a, wrap, affix, plat_f, pure for 5 seconds each
#  affix -  5 wallclock secs ( 5.26 usr +  0.00 sys =  5.26 CPU) @ 695160.46/s (n=3656544)
# plat_a -  4 wallclock secs ( 5.41 usr +  0.00 sys =  5.41 CPU) @ 458635.86/s (n=2481220)
# plat_f -  6 wallclock secs ( 5.25 usr +  0.00 sys =  5.25 CPU) @ 99553.52/s (n=522656)
#   pure -  5 wallclock secs ( 5.24 usr +  0.00 sys =  5.24 CPU) @ 1019883.02/s (n=5344187)
#   wrap -  5 wallclock secs ( 5.21 usr +  0.00 sys =  5.21 CPU) @ 688589.64/s (n=3587552)
#              Rate plat_f plat_a  wrap affix  pure
# plat_f    99554/s    --  -78%  -86%  -86%  -90%
# plat_a   458636/s  361%    --  -33%  -34%  -55%
# wrap     688590/s  592%   50%    --   -1%  -32%
# affix    695160/s  598%   52%    1%    --  -32%
# pure    1019883/s  924%  122%   48%   47%    --
#
# Windows:
# running plat_a, pure, affix, direct, plat_f, wrap for 10 seconds each
# affix - 11 wallclock secs (10.73 usr +  0.02 sys = 10.75 CPU) @ 571958.24/s (n=6149123)
# direct - 10 wallclock secs (10.69 usr +  0.01 sys = 10.70 CPU) @ 651416.71/s (n=6972113)
# plat_a - 11 wallclock secs (10.70 usr +  0.02 sys = 10.72 CPU) @ 209492.96/s (n=2245555)
# plat_f - 10 wallclock secs (10.23 usr +  0.00 sys = 10.23 CPU) @ 46224.15/s (n=473058)
#  pure - 11 wallclock secs (10.39 usr +  0.00 sys = 10.39 CPU) @ 531356.40/s (n=5520793)
#  wrap - 11 wallclock secs (10.38 usr +  0.00 sys = 10.38 CPU) @ 567925.40/s (n=5892226)
#            Rate plat_f plat_a  pure  wrap affix direct
# plat_f   46224/s    --  -78%  -91%  -92%  -92%  -93%
# plat_a  209493/s  353%    --  -61%  -63%  -63%  -68%
# pure    531356/s 1050%  154%    --   -6%   -7%  -18%
# wrap    567925/s 1129%  171%    7%    --   -1%  -13%
# affix   571958/s 1137%  173%    8%    1%    --  -12%
# direct  651417/s 1309%  211%   23%   15%   14%    --
