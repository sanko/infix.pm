use v5.40;
use blib;
use Test2::Tools::Affix qw[:all];
use Affix               qw[affix wrap pin callback load_library find_symbol get_last_error_message sizeof];
use Config;

# Ensure output is not buffered, for clear test diagnostics.
$|++;

# This C code will be compiled into a temporary library for many of the tests.
my $C_CODE = <<'END_C';
#include <stdint.h>
#include <stdbool.h>
#include <string.h> // For strcmp

// Using a C99 style header to ensure fixed-width types are available.
#if defined(_MSC_VER)
    #define DLLEXPORT __declspec(dllexport)
#else
    #define DLLEXPORT
#endif

/* Expose global vars */
DLLEXPORT int global_counter = 42;

/* Basic Primitives */
DLLEXPORT int add(int a, int b) { return a + b; }
DLLEXPORT unsigned int u_add(unsigned int a, unsigned int b) { return a + b; }

// Functions to test every supported primitive type
DLLEXPORT int8_t echo_s8(int8_t v) { return v; }
DLLEXPORT uint8_t echo_u8(uint8_t v) { return v; }
DLLEXPORT int16_t echo_s16(int16_t v) { return v; }
DLLEXPORT uint16_t echo_u16(uint16_t v) { return v; }
DLLEXPORT int32_t echo_s32(int32_t v) { return v; }
DLLEXPORT uint32_t echo_u32(uint32_t v) { return v; }
DLLEXPORT int64_t echo_s64(int64_t v) { return v; }
DLLEXPORT uint64_t echo_u64(uint64_t v) { return v; }
DLLEXPORT float echo_float(float v) { return v; }
DLLEXPORT double echo_double(double v) { return v; }
DLLEXPORT bool echo_bool(bool v) { return v; }

/* Pointers and References */
DLLEXPORT const char* get_hello_string() { return "Hello from C"; }

// Dereferences a pointer and returns its value + 10.
DLLEXPORT int deref_and_add(int* p) {
    if (!p) return -1;
    return *p + 10;
}

// Modifies the integer pointed to by the argument.
DLLEXPORT void modify_int_ptr(int* p, int new_val) {
    if (p) *p = new_val;
}

// Takes a pointer to a pointer and verifies the string.
DLLEXPORT int check_string_ptr(char** s) {
    if (s && *s && strcmp(*s, "perl") == 0) {
        return 1; // success
    }
    return 0; // failure
}

/* Structs and Arrays */
typedef struct {
    int32_t id;
    double value;
    const char* label;
} MyStruct;

// "Constructor" for the struct.
DLLEXPORT void init_struct(MyStruct* s, int32_t id, double value, const char* label) {
    if (s) {
        s->id = id;
        s->value = value;
        s->label = label;
    }
}

// "Getter" for a struct member.
DLLEXPORT int32_t get_struct_id(MyStruct* s) {
    return s ? s->id : -1;
}

// Sums an array of 64-bit integers.
DLLEXPORT int64_t sum_s64_array(int64_t* arr, int len) {
    int64_t total = 0;
    for (int i = 0; i < len; i++) {
        total += arr[i];
    }
    return total;
}

/* Callbacks */
// A harness function for testing simple callbacks
DLLEXPORT int call_int_cb(int (*cb)(int), int val) {
    return cb(val);
}

// Harness for a callback with multiple args and a different return type.
DLLEXPORT double call_math_cb(double (*cb)(double, int), double d, int i) {
    return cb(d, i);
}
END_C

# Compile the library once for all subtests that need it.
my $lib_path = compile_ok($C_CODE);
ok( $lib_path && -e $lib_path, 'Compiled a test shared library successfully' );
#
subtest 'Library Loading and Lifecycle' => sub {
    plan 5;
    note 'Testing load_library(), Affix::Lib objects, and reference counting.';
    my $lib1 = load_library($lib_path);
    isa_ok( $lib1, ['Affix::Lib'], 'load_library returns an Affix::Lib object' );
    my $lib2 = load_library($lib_path);
    is( 0 + $lib1, 0 + $lib2, 'Loading the same library returns a handle to the same underlying object (singleton behavior)' );
    my $bad_lib = load_library('non_existent_library_12345.so');
    is( $bad_lib, undef, 'load_library returns undef for a non-existent library' );
    my $err = get_last_error_message();
    like( $err, qr/found|cannot open|no such file/i, 'get_last_error_message provides a useful error on failed load' );

    # The DESTROY methods for $lib1 and $lib2 will be tested implicitly by Valgrind/ASan for leaks.
    pass('Library objects will be destroyed automatically at scope exit');
};
#
subtest 'Symbol Finding' => sub {
    plan 3;
    my $lib    = load_library($lib_path);
    my $symbol = find_symbol( $lib, 'add' );
    isa_ok( $symbol, ['Affix::Lib'], 'find_symbol returns an Affix::Lib object' );
    my $bad_symbol = find_symbol( $lib, 'non_existent_symbol_12345' );
    is( $bad_symbol, undef, 'find_symbol returns undef for a non-existent symbol' );
    pass('Pin object from find_symbol will be destroyed at scope exit');
};
#
subtest 'Pinning and Marshalling (Dereferencing)' => sub {
    plan 4;
    subtest 'sint32' => sub {
        plan 3;
        my $pin_int;
        pin( $pin_int, $lib_path, 'global_counter', 'int32' );
        diag $pin_int;

        #~ isa_ok( $pin_int, ['Affix::Pin'], 'pin("sint32", ...) returns a pin object' );
        #~ is( ${$pin_int}, 123, 'Dereferencing a pinned int reads the correct value' );
        #~ ${$pin_int} = 456;
        #~ is( ${$pin_int}, 456, 'Assigning to a dereferenced pin writes the correct value' );
    };

    #~ subtest 'double (float64)' => sub {
    #~ plan 3;
    #~ my $pin_double = pin( 'float64', 3.14 );
    #~ isa_ok( $pin_double, ['Affix::Pin'], 'pin("float64", ...) returns a pin object' );
    #~ is( ${$pin_double}, float(3.14), 'Dereferencing a pinned double reads the correct value' );
    #~ ${$pin_double} = -6.28;
    #~ is( ${$pin_double}, float(-6.28), 'Assigning to a dereferenced pin writes the correct value' );
    #~ };
    #~ subtest 'pointer' => sub {
    #~ plan 3;
    #~ my $pin_ptr = pin( '*void', 0xDEADBEEF );
    #~ isa_ok( $pin_ptr, ['Affix::Pin'], 'pin("*void", ...) returns a pin object' );
    #~ is( ${$pin_ptr}, 0xDEADBEEF, 'Dereferencing a pinned pointer reads the correct address' );
    #~ ${$pin_ptr} = 0xCAFEF00D;
    #~ is( ${$pin_ptr}, 0xCAFEF00D, 'Assigning to a dereferenced pin writes the correct address' );
    #~ };
    #~ subtest 'Error on invalid signature' => sub {
    #~ plan 1;
    #~ # Test that pin dies with an invalid signature
    #~ like dies { pin( 'invalid_type_!!', 1 ) }, qr/invalid/, 'pin() croaks on invalid type signature';
    #~ };
};
done_testing;
__END__
#
subtest 'Forward Calls: Comprehensive Primitives' => sub {
    plan 10;
    note 'Testing all primitive fixed-width types from signatures.md.';
    my %tests = (
        s8     => { val => -100,           sig => 'sint8(sint8)' },
        u8     => { val =>  200,           sig => 'uint8(uint8)' },
        s16    => { val => -30000,         sig => 'sint16(sint16)' },
        u16    => { val =>  60000,         sig => 'uint16(uint16)' },
        s32    => { val => -2_000_000_000, sig => 'sint32(sint32)' },
        u32    => { val =>  4_000_000_000, sig => 'uint32(uint32)' },
        s64    => { val => -5_000_000_000, sig => 'sint64(sint64)' },
        u64    => { val => 10_000_000_000, sig => 'uint64(uint64)' },
        float  => { val => float( 1.23),   sig => 'float32(float32)' },
        double => { val => float(-4.56),   sig => 'float64(float64)' },
    );
    for my $type ( sort keys %tests ) {
        my $name = "echo_$type";
        my $val  = $tests{$type}{val};
        my $sig  = $tests{$type}{sig};
        my $func = affix( $lib_path, $name, $sig );
        is( $func->($val), $val, "Correctly passed and returned type '$type'" );
    }
    my $echo_bool = affix( $lib_path, 'echo_bool', 'bool(bool)' );
    is( $echo_bool->(1), 1, "Correctly passed and returned boolean (true)" );
    is( $echo_bool->(0), 0, "Correctly passed and returned boolean (false)" );
};
#
subtest 'Forward Calls: Pointers and References' => sub {
    plan 4;
    note 'Testing pointer marshalling, including pass-by-reference.';
    my $get_string  = affix( $lib_path, 'get_hello_string', '()->*char' );
    my $string_addr = $get_string->();

    # We can't easily dereference this raw address in pure Perl,
    # but we can verify it's a non-zero integer (a valid-looking address).
    is( ref($string_addr), '', 'Returned pointer is a scalar integer' );
    ok( $string_addr > 0, 'Returned pointer is a non-null address' );
    my $deref   = affix( $lib_path, 'deref_and_add', 'sint32(*sint32)' );
    #~ my $int_pin = pin( 'sint32', 50 );
    #~ is( $deref->($int_pin), 60, 'Correctly passed a pin, C dereferenced it and returned value' );
    #~ my $modify = affix( $lib_path, 'modify_int_ptr', 'void(*sint32, sint32)' );
    #~ $modify->( $int_pin, 999 );
    #~ is( ${$int_pin}, 999, 'C function correctly modified the value in our pin (pass-by-reference)' );
};
#
subtest 'Forward Calls: Structs and Arrays' => sub {
    plan 3;
    note 'Testing passing pointers to complex data structures.';

    # The signature for MyStruct is '{s32, f64, p}'
    my $struct_pin = pin('{int32, float64, *char}');
    isa_ok( $struct_pin, ['Affix::Pin'], 'Pinned memory for a struct' );
    my $init   = affix( $lib_path, 'init_struct',   '(*void, int32, float64, *void)->void' );
    my $get_id = affix( $lib_path, 'get_struct_id', '(*void)->int32' );
    my $label  = "Test Label";
    $init->( $struct_pin, 42, 3.14, $label );
    is( $get_id->($struct_pin), 42, 'Struct pointer passed and member retrieved correctly' );
    my $sum_array    = affix( $lib_path, 'sum_s64_array', '(*void, int32->int64)' );
    my @numbers      = ( 100, 200, 300, 400 );
    my $packed_array = pack( "q!*", @numbers );                                        # "q" is signed 64-bit native order
    is( $sum_array->( $packed_array, 4 ), 1000, 'Packed string buffer passed as an array pointer correctly' );
};
#
subtest 'Advanced Callbacks (Reverse FFI)' => sub {
    plan 4;
    note 'Testing creation and use of callbacks with various signatures.';

    # Test 1: Simple callback (already in original tests)
    my $multiplier      = 10;
    my $perl_sub_simple = sub ($input) {

        # This subtest is inside another, so plan is tricky.
        # We just verify it works.
        $input * $multiplier;
    };
    my $harness1 = wrap( $lib_path, 'call_int_cb', '(*((sint32)->sint32), sint32)->sint32' );
    ok $harness1, 'Wrapped the simple C callback harness';
    my $cb1     = callback( '(sint32)->sint32', $perl_sub_simple );
    my $result1 = $harness1->( $cb1, 7 );
    is $result1, 70, 'Simple callback was called by C code and returned the correct result';

    # Test 2: Callback with multiple arguments and float return
    my $perl_sub_math = sub ( $d, $i ) {

        # Verify arguments received from C
        is $d, float(1.5), 'Callback received correct double argument from C';
        return $d**$i;    # 1.5 ^ 3
    };
    my $harness2 = affix( $lib_path, 'call_math_cb', 'f64(*((f64,s32)->f64), f64, s32)' );
    my $cb2      = callback( '(f64,s32)->f64', $perl_sub_math );
    my $result2  = $harness2->( $cb2, 1.5, 3 );
    is( $result2, float(3.375), 'Callback with multiple args and float return worked correctly' );
};
#
subtest 'Error Handling on Invalid Signatures' => sub {
    plan 2;
    note "Testing that affix() croaks with bad signatures.";
    like dies {
        affix( $lib_path, 'add', 'this is not a valid signature' );
    }, qr[valid], 'affix() croaks on a completely invalid signature';
    like dies {
        affix( $lib_path, 'add', '(sint32, sint32)->invalid_return' );
    }, qr[invalid], 'affix() croaks on an invalid type within a signature';
};

# In your test script, before done_testing;
subtest 'sizeof() Introspection' => sub {
    plan 7;
    note 'Testing the native Affix::sizeof() function.';

    # Basic primitives
    is( sizeof('int8'),    1, "sizeof('int8') is 1" );
    is( sizeof('int32'),   4, "sizeof('int32') is 4" );
    is( sizeof('float64'), 8, "sizeof('float64') is 8" );

    # Pointer size should match the platform's pointer size
    is( sizeof('*void'), $Config{ptrsize}, "sizeof('*void') matches platform pointer size" );

    # Struct with padding
    # C struct { int8_t a; int32_t b; };
    # Layout: char (1) + padding (3) + int (4) = 8 bytes
    is( sizeof('{int8, int32}'), 8, 'sizeof() correctly calculates struct size with padding' );

    # Packed struct (no padding)
    # C struct __attribute__((packed)) { int8_t a; int32_t b; };
    # Layout: char (1) + int (4) = 5 bytes
    is( sizeof('!{int8, int32}'), 5, 'sizeof() correctly calculates packed struct size' );

    # Array
    # C double arr[10]; -> 10 * 8 = 80 bytes
    is( sizeof('[10:double]'), 80, 'sizeof() correctly calculates array size' );
};
#
done_testing;
exit;
__END__
use v5.40;
use blib;
use Test2::Tools::Affix qw[:all];
use Affix               qw[affix wrap pin unpin load_library find_symbol get_last_error_message];

# Ensure output is not buffered, for clear test diagnostics.
$|++;

# This C code will be compiled into a temporary library for many of the tests.
my $C_CODE = <<'END_C';
#include "std.h"
// ext: .c

// For basic affix tests
DLLEXPORT int add(int a, int b) { return a + b; }
DLLEXPORT unsigned int u_add(unsigned int a, unsigned int b) { return a + b; }

// For symbol finding tests
DLLEXPORT int get_42() { return 42; }

// A harness function for testing callbacks
DLLEXPORT int call_int_cb(int (*cb)(int), int val) {
    return cb(val);
}
END_C

# Compile the library once for all subtests that need it.
my $lib_path = compile_ok($C_CODE);
ok( $lib_path && -e $lib_path, 'Compiled a test shared library successfully' );
#
subtest 'Library Loading and Lifecycle' => sub {
    plan 5;
    note 'Testing load_library(), Affix::Lib objects, and reference counting.';
    my $lib1 = load_library($lib_path);
    isa_ok( $lib1, ['Affix::Lib'], 'load_library returns an Affix::Lib object' );
    my $lib2 = load_library($lib_path);
    is( 0 + $lib1, 0 + $lib2, 'Loading the same library returns a handle to the same underlying object (singleton behavior)' );
    my $bad_lib = load_library('non_existent_library_12345.so');
    is( $bad_lib, undef, 'load_library returns undef for a non-existent library' );
    my $err = get_last_error_message();
    like( $err, qr/found/i, 'get_last_error_message provides a useful error on failed load' );

    # The DESTROY methods for $lib1 and $lib2 will be tested implicitly by Valgrind/ASan for leaks.
    pass('Library objects will be destroyed automatically at scope exit');
};
subtest 'Symbol Finding' => sub {
    plan 3;
    note 'Testing find_symbol() returns a blessed Affix::Pin object.';
    my $lib    = load_library($lib_path);
    my $symbol = find_symbol( $lib, 'get_42' );
    isa_ok( $symbol, ['Affix::Pin'], 'find_symbol returns an Affix::Pin object' );
    my $bad_symbol = find_symbol( $lib, 'non_existent_symbol_12345' );
    is( $bad_symbol, undef, 'find_symbol returns undef for a non-existent symbol' );
    pass('Pin object from find_symbol will be destroyed at scope exit');
};
subtest 'Pinning and Marshalling (Dereferencing)' => sub {
    plan 2;
    note 'Testing pin() and the magic of ${...} for reading/writing C data.';
    subtest 'int32' => sub {
        plan 3;
        my $pin_int = pin( 'int32', 123 );
        isa_ok( $pin_int, ['Affix::Pin'], 'pin("int32", ...) returns a pin object' );
        is( ${$pin_int}, 123, 'Dereferencing a pinned int reads the correct value' );
        ${$pin_int} = 456;
        is( ${$pin_int}, 456, 'Assigning to a dereferenced pin writes the correct value' );
    };
    subtest 'double' => sub {
        plan 3;
        my $pin_double = pin( 'double', 3.14 );
        isa_ok( $pin_double, ['Affix::Pin'], 'pin("double", ...) returns a pin object' );
        is( ${$pin_double}, float(3.14), 'Dereferencing a pinned double reads the correct value' );
        ${$pin_double} = -6.28;
        is( ${$pin_double}, float(-6.28), 'Assigning to a dereferenced pin writes the correct value' );
    };
};
subtest 'Original Forward Call Tests' => sub {
    plan 2;
    subtest 'int32 add(int32, int32)' => sub {
        plan 2;
        ok affix( $lib_path, 'add', '(int32, int32) -> int32' ), 'affix( "add", "(int32, int32) -> int32")';
        is add( 10, 4 ), 14, 'add( 10, 4 )';
    };
    subtest 'uint32 u_add(uint32, uint32)' => sub {
        plan 2;
        ok affix( $lib_path, 'u_add', '(uint32, uint32) -> uint32' ), 'affix( "u_add", "(uint32, uint32) -> uint32")';
        is u_add( 10, 4 ), 14, 'u_add( 10, 4 )';
    };
};
#
subtest 'Callbacks (Reverse FFI)' => sub {
    plan 3;
    note 'Testing creation and use of a callback from a Perl sub.';
    my $multiplier = 10;
    my $perl_sub   = sub ($input) {
        is $input, 7, 'inside callback';
        $input * $multiplier;
    };
    my $harness = wrap( $lib_path, 'call_int_cb', '(*((int)->int), int)->int' );
    ok $harness, 'Wrapped the C harness function';
    my $result = $harness->( Affix::callback( '(int)->int', $perl_sub ), 7 );
    is $result, 70, 'Callback was called by C code and returned the correct result';
};
#
done_testing;
exit;
