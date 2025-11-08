use v5.36;
use lib '../lib', 'lib';
use blib;
use Test2::Tools::Affix qw[:all];
use Affix               qw[:all];

# --- Setup: Compile a dedicated test library for these tests ---
my $C_CODE   = do { local $/; <DATA> };
my $lib_path = compile_ok($C_CODE);
ok( -e $lib_path, 'Successfully compiled the extended test library' );

# --- Setup: Define types used across multiple tests ---
typedef <<'END_TYPEDEFS';
    @Point   = { x: int, y: int };
    @MyUnion = < i: int, f: float, c: [8:char] >;
    @Rect    = { a: @Point, b: @Point };
END_TYPEDEFS

# =====================================================================
subtest 'Memory Management (malloc, calloc, free)' => sub {

    # =====================================================================
    my $ptr = malloc(32);
    ok $ptr, 'malloc returns a pinned SV*';
    use Data::Printer;
    p $ptr;
    diag length $ptr;
    diag Affix::dump( $ptr, 32 );
    ok my $array_ptr = calloc( 4, 'int' ), 'calloc returns an array';
    diag Affix::dump( $array_ptr, 32 );
    ok $array_ptr, 'calloc returns an Affix::Pointer object';
    ok affix $lib_path, 'sum_int_array', '(*int, int)->int';
    is sum_int_array( $array_ptr, 4 ), 0, 'Memory from calloc is zero-initialized';
    ok free($array_ptr), 'Explicitly calling free() returns true';

    # Note: Double-free would crash, so we assume it worked.
    like( dies { free( find_symbol( load_library($lib_path), 'sum_int_array' ) ) },
        qr/unmanaged/, 'free() croaks when called on an unmanaged pointer' );

    # Test that auto-freeing via garbage collection doesn't crash
    subtest 'GC of managed pointers' => sub {
        ok my $scoped_ptr = malloc(16), 'malloc(16)';

        #~ ok cast( $scoped_ptr, '*int'), 'cast void pointer to int pointer';
        #~ ${$scoped_ptr} = 99;    # Write to it to make sure it's valid
        Affix::dump( $scoped_ptr, 32 );
        diag '[' . $scoped_ptr . ']';

        # When $scoped_ptr goes out of scope here, its DESTROY method is called.
    };
    pass('Managed pointer went out of scope without crashing');
};

=fdsa
# =====================================================================
subtest 'Affix::Pointer Methods (cast, realloc, deref)' => sub {

    # =====================================================================
    affix $lib_path, 'read_int_from_void_ptr', '(*void)->int';
    my $mem = malloc(8);
    $mem->cast('*int');

    # Test magical 'set' via dereferencing
    ${$mem} = 42;
    is( read_int_from_void_ptr($mem), 42, 'Magical set via deref wrote to C memory' );

    # Test cast again
    $mem->cast('*longlong');
    ${$mem} = 1234567890123;
    is( ${$mem}, 1234567890123, 'Magical get after casting to a new type works' );

    # Test realloc
    my $r_ptr = calloc( 2, 'int' );
    $r_ptr->realloc(32);    # Reallocate to hold 8 ints
    $r_ptr->cast('[8:int]');
    $r_ptr->[0] = 10;
    $r_ptr->[7] = 80;
    is( sum_int_array( $r_ptr, 8 ), 90, 'realloc successfully resized memory' );
};

# =====================================================================
subtest 'Advanced Structs and Unions' => sub {

    # =====================================================================
    affix $lib_path, 'sum_point_by_val', '(@Point)->int';
    my $point_hash = { x => 10, y => 25 };
    is( sum_point_by_val($point_hash), 35, 'Correctly passed a struct by value' );
    affix $lib_path, 'read_union_int', '(@MyUnion)->int';
    my $union_hash = { i => 999 };
    is( read_union_int($union_hash), 999, 'Correctly read int member from a C union' );
};

# =====================================================================
subtest 'Advanced Arrays' => sub {

    # =====================================================================
    affix $lib_path, 'get_char_at', '([20:char], int)->char';
    my $str = "Perl";
    is( chr( get_char_at( $str, 0 ) ), 'P', 'Passing string to char[N] works (char 0)' );
    is( get_char_at( $str, 4 ),        0,   'Passing string to char[N] is null-terminated' );
    my $long_str = "This is a very long string that will be truncated";
    is( chr( get_char_at( $long_str, 18 ) ), 'g', 'Truncated string char 18 is correct' );
    is( get_char_at( $long_str, 19 ),        0,   'Truncated string is null-terminated at the boundary' );
    affix $lib_path, 'sum_float_array', '(*float, int)->float';
    my $floats = [ 1.1, 2.2, 3.3 ];
    is( sum_float_array( $floats, 3 ), float( 6.6, tolerance => 0.01 ), 'Correctly summed an array of floats' );
};

# =====================================================================
subtest 'Advanced Pointers and NULLs' => sub {

    # =====================================================================
    affix $lib_path, 'modify_int_ptr_ptr', '(**int, int)->void';
    my $val = 100;
    my $ptr = \$val;
    modify_int_ptr_ptr( \$ptr, 555 );
    is( $val, 555, 'Correctly modified a Perl scalar via an int**' );
    affix $lib_path, 'return_null_ptr', '()->*int';
    my $null_ptr = return_null_ptr();
    isa_ok $null_ptr, ['Affix::Pointer'], 'A C function returning NULL gives a valid pointer object';
    is( ${$null_ptr}, U(), 'Dereferencing the null pointer object results in undef' );
};

# =====================================================================
subtest 'Callback Edge Cases' => sub {
    affix $lib_path, 'call_void_cb', '(*(()->void))->void';
    like dies {
        call_void_cb( sub { die "It works!" } )
    }, qr/It works!/

        #'A Perl exception from a callback is caught and turned into a warning'
        ;
    affix $lib_path, 'check_cb_returns_zero', '(*(()->int))->int';
    my $result = check_cb_returns_zero( sub { return undef } );
    is( $result, 1, 'Callback returning undef is received as 0 in C' );
};

# =====================================================================
subtest 'sizeof Operator' => sub {

    # =====================================================================
    # From typedef: @Rect = { a: @Point, b: @Point }; @Point = {x:int, y:int}
    my $size_of_int = sizeof('int');
    is( sizeof('@Rect'), $size_of_int * 4, 'sizeof works correctly on complex named types' );
};

=cut

done_testing;
__DATA__
#include "std.h"
//ext: .c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* --- Structs and Unions from 001_affix.t for reuse --- */
typedef struct {
    int x;
    int y;
} Point;

typedef union {
    int i;
    float f;
    char c[8];
} MyUnion;


/* --- Section 1: Memory and Pointer Helpers --- */

// Reads an integer from a raw memory address.
DLLEXPORT int read_int_from_void_ptr(void* p) {
    if (!p) return -999;
    return *(int*)p;
}

// Reads and sums `count` integers from an array.
DLLEXPORT int sum_int_array(int* arr, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += arr[i];
    }
    return total;
}

// Dereferences an int** and modifies the original value.
DLLEXPORT void modify_int_ptr_ptr(int** p, int val) {
    if (p && *p) {
        **p = val;
    }
}

// A function that explicitly returns NULL.
DLLEXPORT int* return_null_ptr(void) {
    return NULL;
}


/* --- Section 2: Structs, Unions, Arrays --- */

// Takes a struct BY VALUE and returns the sum of its members.
DLLEXPORT int sum_point_by_val(Point p) {
    return p.x + p.y;
}

// Reads the integer member of a union passed from Perl.
DLLEXPORT int read_union_int(MyUnion u) {
    return u.i;
}

// Tests passing a Perl string to a fixed-size C char array.
// Returns the character at the requested index.
DLLEXPORT char get_char_at(char s[20], int index) {
    if (index >= 20 || index < 0) return '!';
    return s[index];
}

// Sums an array of floats.
DLLEXPORT float sum_float_array(float* arr, int len) {
    float total = 0.0f;
    for (int i = 0; i < len; i++)
        total += arr[i];
    return total;
}


/* --- Section 3: Callback Edge Case Helpers --- */

// A harness for testing callbacks that die.
DLLEXPORT void call_void_cb(void (*cb)(void)) {
    if (cb) cb();
}

// A harness for checking if a callback returns 0/NULL.
DLLEXPORT int check_cb_returns_zero(int (*cb)(void)) {
    if (!cb) return -1;
    // Returns 1 if the callback returned 0, otherwise returns 0.
    return cb() == 0 ? 1 : 0;
}
