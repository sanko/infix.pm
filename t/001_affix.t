use v5.40;
use blib;

# Use your new testing toolkit
use Test2::Tools::Affix qw[:all];
use Affix;
#
$|++;
subtest callback => sub {
    subtest 'Passing a Perl Sub as a Callback' => sub {
        my $lib = compile_ok(<<'');
#include "std.h"
// ext: .c
typedef int (*int_callback_t)(int); // A callback function pointer type
DLLEXPORT int run_callback(int_callback_t cb, int value) {
    warn("cb:    %p", cb);
    warn("value: %d", value);
    if (cb == NULL)
        return -1;
    // Call the provided function pointer and return its result.
    return cb(value);
}


        # The C function that will execute our callback
        #~ my $run_callback = affix $lib, 'run_callback', '(i=>i)*,i=>i', undef;
        ok my $run_callback = affix $lib, 'run_callback', '(((int32)->int32),int32)->int32';
        is run_callback( sub { warn 'Hi!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!'; return 100 }, 4 ), 4, 'run_callback(...)';
        diag 'here';

        # Create a native C function pointer from our Perl sub
        #~ my $c_func_ptr = callback( $perl_cb, 'i => i' );
        #~ isa_ok( $c_func_ptr, 'Affix::Pointer', 'callback() returns a pointer object' );
        # Call the C function, passing it our generated callback pointer
        #~ my $result = $run_callback->( $c_func_ptr, 7 );
        #~ is( $result, 70, 'C function correctly executed Perl callback' );
    };
};
done_testing;
exit;

# The compile_ok function builds our C library and returns the path to it.
my $lib_path = compile_ok(<<'END');
#include "std.h"
// ext: .c

#include <string.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct {// A simple struct for testing pass-by-value and return-by-value
    double x;
    double y;
} Point;

DLLEXPORT int add_int(int a, int b) {
    return a + b;
}

DLLEXPORT double sum_double(double a, double b) {
    return a + b;
}

DLLEXPORT size_t string_len(const char* s) {
    if (s == NULL)  return 0;
    return strlen(s);
}

DLLEXPORT char* string_reverse(const char* s) {
    if (s == NULL)
        return NULL;

    size_t len = strlen(s);

    char* rev = (char*)malloc(len + 1); // Leak?
    if (rev == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        rev[i] = s[len - 1 - i];
    }
    rev[len] = '\0';
    return rev;
}

DLLEXPORT void free_string(char* s) {
    if (s != NULL)
        free(s);
        s = NULL;
}

DLLEXPORT double process_point(Point p) {
    // Return a value that depends on both fields to verify it was passed correctly.
    return p.x + p.y;
}

Point get_origin_point(void) {
    Point p = { 1.23, 4.56 };
    return p;
}
END
plan skip_all => "Could not build native test library" unless $lib_path && -f $lib_path;
subtest 'Integer Arguments and Return' => sub {
    ok affix $lib_path, 'add_int', 'i,i => i';
    is add_int(  40, 2 ), 42, 'add_int(40, 2) returns 42';
    is add_int( -10, 5 ), -5, 'add_int(-10, 5) returns -5';
};
subtest 'Double Arguments and Return' => sub {
    affix $lib_path, 'sum_double', 'd,d => d', undef;
    is sum_double( 1.5, 2.75 ), 4.25, 'sum_double(1.5, 2.75) returns 4.25';
};
subtest 'String (char*) Argument' => sub {
    ok affix $lib_path, string_len => 'c* => y';    # y is size_t
    is string_len("hello"), 5, 'string_len("hello") returns 5';
    is string_len(""),      0, 'string_len("") returns 0';
};
subtest 'String (char*) Return' => sub {
    my $string_reverse = affix( $lib_path, 'string_reverse', 'c* => c*', undef );
    my $free_string    = affix( $lib_path, 'free_string',    'c* => v',  undef );
    my $reversed_ptr   = $string_reverse->("world");
    isa_ok( $reversed_ptr, 'Affix::Pointer' );

    # Check that it's unmanaged by default
    is( $reversed_ptr->managed, F(), 'Pointer returned from C is unmanaged' );
    free_string($reversed_ptr);
    pass('Called native free_string() on returned pointer');
};
subtest 'Pointer Methods and Casting' => sub {
    my $int_ptr = Affix::Pointer->new( 'i', 4 );
    isa_ok( $int_ptr, 'Affix::Pointer' );
    $int_ptr->set( 0, 0x44434241 );    # 'ABCD'
    $int_ptr->set( 1, 0x48474645 );    # 'EFGH'
    is( $int_ptr->get(1), 0x48474645, 'set() and get() work correctly' );
    my $char_ptr = $int_ptr->cast('[16]c');
    diag $int_ptr->raw(16);
    diag $char_ptr->raw(16);
    isa_ok( $char_ptr, 'Affix::Pointer::Unmanaged' );
    is( $char_ptr->count, 16, 'Casted pointer has the correct new count (16)' );
    my $got_char = chr( $char_ptr->get(4) );    # Get the 5th byte ('E')
    is( $got_char, 'E', 'Memory read correctly through casted char* view' );
};
done_testing;
exit;
subtest 'Scalar Deref and Iteration' => sub {
    my $ptr = Affix::Pointer->new( 'i', 2 );
    $ptr->set( 0, 55 );
    $ptr->set( 1, 66 );
    is( ${$ptr}, 55, 'Scalar deref ${} reads value at initial position 0' );
    $ptr++;
    is( ${$ptr}, 66, 'Scalar deref reads value at new position 1 after ++' );
};
subtest 'Managed Flag Control' => sub {
    isa_ok my $ptr = Affix::malloc(10), ['Affix::Pointer'];
    is $ptr->managed, T(), 'Pointer from Affix::malloc is managed by default';
    $ptr->managed(0);
    is $ptr->managed, F(), 'managed(0) correctly sets the flag to false';

    # This pointer will now go out of scope. Because it's unmanaged, the C
    # memory it points to will NOT be freed by its DESTROY method, preventing a
    # "free to wrong pool" error if we were to free it manually later.
    # (This is more of a design test than a functional one).
    $ptr->managed(1);
    is( $ptr->managed, 1, 'managed(1) correctly sets the flag back to true' );

    # Now when $ptr goes out of scope, its DESTROY method will free the memory.
};
subtest struct => sub {

    # Define the struct signature with named members for marshalling
    my $point_sig = "{d'x';d'y'}";
    subtest 'Passing Struct by Value' => sub {
        my $process_point = affix $lib_path, process_point => "$point_sig => d";
        my $point         = { x => 10.5, y => 31.5 };
        my $result        = $process_point->($point);
        is( $result, 42.0, 'Struct passed by value and processed correctly' );
    };
    subtest 'Returning Struct by Value' => sub {
        my $get_origin = affix $lib_path, get_origin_point => "v=> $point_sig";
        my $result     = $get_origin->();
        is $result, { x => 1.23, y => 4.56 }, 'Returned struct has correct values';
    };
};
subtest pin => sub {
    my ( $lib, $ver );
    #
    subtest 'setup for pin' => sub {
        $lib = compile_ok(<<'');
#include "std.h"
// ext: .c
DLLEXPORT int VERSION = 100;
DLLEXPORT int get_VERSION(){ return VERSION; }

        isa_ok affix( $lib, 'get_VERSION', 'v=>i' ), ['Affix'];
    };

    # bind an exported value to a Perl value
    ok Affix::pin( $ver, $lib, 'VERSION', 'i' ), 'ping( $ver, ..., "VERSION", Int )';
    is $ver,          100, 'var pulled value from pin( ... )';
    is $ver = 2,      2,   'set var on the perl side';
    is get_VERSION(), 2,   'pin set the value in our library';
};

=fdsa
subtest benchmark => sub {

use FFI::Platypus 2.00;
# for all new code you should use api => 2
my $ffi = FFI::Platypus->new( api => 2, lib => [$lib_path] );

# call dynamically
$ffi->attach( [ add_int => 'add_platypus' ] => [ 'int', 'int' ] => 'int' );
    pass 'wow';
    use Benchmark qw[timethese cmpthese];
    sub add_int_perl( $a, $b ) { $a + $b }
    #
    my $x = 3;
    my $r = timethese(
        -5,
        {   a => sub { add_int( $x, 1 ) },
            b => sub { add_int_perl( $x, 1 ) },
            c => sub { add_platypus( $x, 1 ) }
        }
    );
    cmpthese $r;
};

=cut

done_testing;
