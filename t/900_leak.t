use lib './lib', '../lib', '../blib/arch/', 'blib/arch', '../', '.';
use Affix               qw[:all];
use Test2::Tools::Affix qw[:all];
$|++;
skip_all 'I have no idea why *BSD is leaking here' if $^O =~ /BSD/i;
leaks 'use Affix' => sub {
    use Affix qw[];
    pass 'loaded';
};
leaks 'affix($$$$)' => sub {
    affix_ok libm, 'pow', [ Double, Double ], Double;
    is pow( 5, 2 ), 25, 'pow(5, 2)';
};
leaks 'wrap($$$$)' => sub {
    isa_ok my $pow = wrap( libm, 'pow', [ Double, Double ], Double ), ['Affix'], 'double pow(double, double)';
    is $pow->( 5, 2 ), 25, '$pow->(5, 2)';
};
leaks 'return pointer' => sub {
    my $lib = compile_ok(<<'');
#include "std.h"
// ext: .c
void * test( ) { void * ret = "Testing"; return ret; }

    affix_ok $lib, 'test', [] => Pointer [Void];
    isa_ok my $string = test(), ['Affix::Pointer'], 'test()';
    is $string->raw(7), 'Testing', '->raw(7)';
};
leaks 'return malloc\'d pointer' => sub {
    ok my $lib = compile_ok(<<'');
#include "std.h"
// ext: .c
void * test() {
  void * ret = malloc(8);
  if ( ret == NULL ) { warn("Memory allocation failed!"); }
  else { strcpy(ret, "Testing"); }
  return ret;
}

    affix_ok $lib, 'test', [] => Pointer [Void];
    isa_ok my $string = test(), ['Affix::Pointer'], 'test()';
    is $string->raw(7), 'Testing', '->raw(7)';
    $string->free;
    is $string, U(), '->free() worked';
};
done_testing;
