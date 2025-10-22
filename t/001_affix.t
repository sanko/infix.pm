use v5.40;
use blib;
use Test2::Tools::Affix qw[:all];
use Affix               qw[:all];
use Config;

# Ensure output is not buffered, for clear test diagnostics.
$|++;

# This C code will be compiled into a temporary library for many of the tests.
my $C_CODE = <<'END_C';
#include "std.h"
//ext: .c

#include <stdint.h>
#include <stdbool.h>
#include <string.h> // For strcmp
#include <stdlib.h> // For malloc

/* Expose global vars */
DLLEXPORT int global_counter = 42;
DLLEXPORT void set_global_counter(int value) { global_counter = value;}
DLLEXPORT int get_global_counter(void) { return global_counter;}

/* Basic Primitives */
DLLEXPORT int add(int a, int b) { return a + b; }
DLLEXPORT unsigned int u_add(unsigned int a, unsigned int b) { return a + b; }

// Functions to test every supported primitive type
DLLEXPORT int8_t   echo_int8   (int8_t   v) { return v; }
DLLEXPORT uint8_t  echo_uint8  (uint8_t  v) { return v; }
DLLEXPORT int16_t  echo_int16  (int16_t  v) { return v; }
DLLEXPORT uint16_t echo_uint16 (uint16_t v) { return v; }
DLLEXPORT int32_t  echo_int32  (int32_t  v) { return v; }
DLLEXPORT uint32_t echo_uint32 (uint32_t v) { return v; }
DLLEXPORT int64_t  echo_int64  (int64_t  v) { return v; }
DLLEXPORT uint64_t echo_uint64 (uint64_t v) { return v; }
DLLEXPORT float    echo_float  (float    v) { return v; }
DLLEXPORT double   echo_double (double   v) { return v; }
DLLEXPORT bool     echo_bool   (bool     v) { return v; }

/* Pointers and References */
DLLEXPORT const char* get_hello_string() { return "Hello from C"; }
DLLEXPORT bool set_hello_string(const char * hi) { return strcmp(hi, "Hello from Perl")==0; }

// Dereferences a pointer and returns its value + 10.
DLLEXPORT int deref_and_add(int* p) {
    if (!p) return -1;
    return *p + 10;
}

// Modifies the integer pointed to by the argument.
DLLEXPORT void modify_int_ptr(int* p, int new_val) {
    if (p) *p = new_val + 1;
}

// Takes a pointer to a pointer and verifies the string.
DLLEXPORT int check_string_ptr_ptr(char** s) {
    if (s && *s && strcmp(*s, "perl") == 0) {
        // Modify the inner pointer to prove we can
        *s = "C changed me";
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
    for (int i = 0; i < len; i++)
        total += arr[i];
    return total;
}

// Returns a pointer to a static internal struct
MyStruct g_struct = { 99, -1.0, "Global" };
DLLEXPORT MyStruct* get_static_struct_ptr() {
    return &g_struct;
}

/* Nested Structs */
typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point top_left;
    Point bottom_right;
    const char* name;
} Rectangle;

DLLEXPORT int get_rect_width(Rectangle* r) {
    if (!r) return -1;
    return r->bottom_right.x - r->top_left.x;
}

// Return a struct by value
DLLEXPORT Point create_point(int x, int y) {
    Point p = {x, y};
    return p;
}

/* Advanced Pointers */
DLLEXPORT bool check_is_null(void* p) {
    return (p == NULL);
}
// Takes a void* and casts it to an int*
DLLEXPORT int read_int_from_void_ptr(void* p) {
    if (!p) return -999;
    return *(int*)p;
}

/* Arrays of Structs */
DLLEXPORT int sum_struct_ids(MyStruct* structs, int count) {
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += structs[i].id;
    }
    return total;
}

/* Enums and Unions */
typedef enum { RED, GREEN, BLUE } Color;

DLLEXPORT int check_color(Color c) {
    if (c == GREEN) return 1;
    return 0;
}

typedef union {
    int i;
    float f;
    char c[8];
} MyUnion;

DLLEXPORT float process_union_float(MyUnion u) {
    return u.f * 10.0;
}

/* Advanced Callbacks */
// Takes a callback that processes a struct
DLLEXPORT double process_struct_with_cb(MyStruct* s, double (*cb)(MyStruct*)) {
    return cb(s);
}

// Takes a callback that returns a struct
DLLEXPORT int check_returned_struct_from_cb(Point (*cb)(void)) {
    Point p = cb();
    return p.x + p.y;
}

// A callback with many arguments to test register/stack passing
typedef void (*kitchen_sink_cb)(
    int a, double b, int c, double d, int e, double f, int g, double h,
    const char* i, int* j
);
DLLEXPORT void call_kitchen_sink(kitchen_sink_cb cb) {
    int j_val = 100;
    cb(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0, "kitchen sink", &j_val);
}

/* Functions with many arguments */
DLLEXPORT long long multi_arg_sum(
    long long a, long long b, long long c, long long d,
    long long e, long long f, long long g, long long h, long long i
) {
    return a + b + c + d + e + f + g + h + i;
}

/* Simple Callback Harness */
DLLEXPORT int call_int_cb(int (*cb)(int), int val) {
    return cb(val);
}

DLLEXPORT double call_math_cb(double (*cb)(double, int), double d, int i) {
    return cb(d, i);
}
END_C

# Compile the library once for all subtests that need it.
my $lib_path = compile_ok($C_CODE);
ok( $lib_path && -e $lib_path, 'Compiled a test shared library successfully' );
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
    like( $err, qr/failed/i, 'get_last_error_message provides a useful error on failed load' );
    pass('Library objects will be destroyed automatically at scope exit');
};
subtest 'Symbol Finding' => sub {
    plan 2;
    my $lib    = load_library($lib_path);
    my $symbol = find_symbol( $lib, 'add' );
    isa_ok( $symbol, ['Affix::Pointer'], 'find_symbol returns an Affix::Pointer object' );
    is find_symbol( $lib, 'non_existent_symbol_12345' ), U(), 'find_symbol returns undef for a non-existent symbol';
};
subtest 'Pinning and Marshalling (Dereferencing)' => sub {
    plan 1;
    subtest 'sint32' => sub {
        plan 9;
        my $pin_int;
        isa_ok affix( $lib_path, 'get_global_counter', '()->int32' );
        isa_ok affix( $lib_path, 'set_global_counter', '(int32)->void' );
        ok pin( $pin_int, $lib_path, 'global_counter', 'int32' ), 'pin(...)';
        is $pin_int, 42, 'pinned scalar equals 42';
        diag 'setting pinned scalar to 100';
        $pin_int = 100;
        is get_global_counter(), 100, 'checking value from inside the shared lib';
        diag 'setting value from inside the shared lib';
        set_global_counter(200);
        is $pin_int, 200, 'checking value from perl';
        diag 'unpinning scalar';
        ok unpin($pin_int), 'unpin() returns true';
        diag 'setting unpinned scalar to 25';
        $pin_int = 25;
        is get_global_counter(), 200, 'value is unchanged inside the shared lib';
        is $pin_int,             25,  'verify that value is local to perl';
    };
};
subtest 'Forward Calls: Comprehensive Primitives' => sub {
    for my ( $type, $value )(
        bool  => false,                                       #
        int8  => -100,           uint8  => 100,               #
        int16 => -30000,         uint16 => 60000,             #
        int32 => -2_000_000_000, uint32 => 4_000_000_000,     #
        int64 => -5_000_000_000, uint64 => 10_000_000_000,    #
        float =>  1.23,          double => -4.56              #
    ) {
        my $name = "echo_$type";
        my $sig  = "($type)->$type";
        isa_ok my $fn = wrap( $lib_path, $name, $sig ), ['Affix'], $sig;
        is( $fn->($value), $value == int $value ? $value : float( $value, tolerance => 0.01 ), "Correctly passed and returned type '$type'" );
    }
};
subtest 'Forward Calls: Comprehensive Pointer Types' => sub {
    plan 8;
    isa_ok my $check_is_null = wrap( $lib_path, 'check_is_null', '(*void)->bool' ), ['Affix'];
    ok $check_is_null->(undef), 'Passing undef to a *void argument is received as NULL';
    subtest 'char*' => sub {
        plan 4;
        isa_ok my $get_string = wrap( $lib_path, 'get_hello_string', '()->*char' ), ['Affix'];
        is $get_string->(), 'Hello from C', 'Correctly returned a C string';
        isa_ok my $set_string = wrap( $lib_path, 'set_hello_string', '(*char)->bool' ), ['Affix'];
        ok $set_string->('Hello from Perl'), 'Correctly passed a string to C';
    };
    subtest 'int32*' => sub {
        plan 4;
        isa_ok my $deref  = wrap( $lib_path, 'deref_and_add',  '(*int32)->int32' ),       ['Affix'];
        isa_ok my $modify = wrap( $lib_path, 'modify_int_ptr', '(*int32, int32)->void' ), ['Affix'];
        my $int_var = 50;
        is $deref->( \$int_var ), 60, 'Passing a scalar ref as an "in" pointer works';
        $modify->( \$int_var, 999 );
        is $int_var, 1000, 'C function correctly modified the value in our scalar ref ("out" param)';
    };
    subtest 'void*' => sub {
        plan 2;
        isa_ok my $read_void = wrap( $lib_path, 'read_int_from_void_ptr', '(*void)->int32' ), ['Affix'];
        my $int_val = 12345;
        is $read_void->( \$int_val ), 12345, 'Correctly passed a scalar ref as a void* and read its value';
    };
    subtest 'char**' => sub {
        plan 3;
        isa_ok my $check_ptr_ptr = wrap( $lib_path, 'check_string_ptr_ptr', '(**char)->int32' ), ['Affix'];
        my $string = 'perl';
        ok $check_ptr_ptr->( \$string ), 'Correctly passed a reference to a string as char**';
        is $string, 'C changed me', 'C function was able to modify the inner pointer';
    };
    subtest 'Struct Pointers (*@MyStruct)' => sub {
        plan 6;
        ok typedef('@My::Struct = { id: int32, value: float64, label: *char };'), 'typedef("@My::Struct = ...")';
        isa_ok my $init_struct = wrap( $lib_path, 'init_struct', '(*@My::Struct, int32, float64, *char)->void' ), ['Affix'];
        my %struct_hash;
        $init_struct->( \%struct_hash, 101, 9.9, "Initialized" );
        is \%struct_hash, { id => 101, value => 9.9, label => "Initialized" }, 'Correctly initialized a Perl hash via a struct pointer';
        isa_ok my $get_ptr = wrap( $lib_path, 'get_static_struct_ptr', '()->*@My::Struct' ), ['Affix'];
        my $struct_ptr = $get_ptr->();
        isa_ok $struct_ptr, ['Affix::Pointer'], 'Receiving a struct pointer returns an Affix::Pointer object';
        is $$struct_ptr, { id => 99, value => -1.0, label => 'Global' }, 'Dereferencing a returned struct pointer works';
    };
    subtest 'Function Pointers (*(int->int))' => sub {
        plan 3;
        isa_ok my $harness = wrap( $lib_path, 'call_int_cb', '(*((int32)->int32), int32)->int32' ), ['Affix'];
        my $result = $harness->( sub { $_[0] * 10 }, 7 );
        is $result, 70, 'Correctly passed a simple coderef as a function pointer';
        ok $check_is_null->(undef), 'Passing undef as a function pointer is received as NULL';
    };
};
subtest 'Forward Call with Many Arguments' => sub {
    plan 2;
    note 'Testing a C function with more arguments than available registers.';
    my $sig = '(int64, int64, int64, int64, int64, int64, int64, int64, int64)->int64';
    isa_ok my $summer = wrap( $lib_path, 'multi_arg_sum', $sig ), ['Affix'];
    my $result = $summer->( 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 );
    is $result, 111111111, 'Correctly passed 9 arguments to a C function';
};
subtest 'Parser Error Reporting' => sub {
    plan 2;
    note 'Testing that malformed signatures produce helpful error messages.';
    like dies { Affix::wrap( $lib_path, 'add', '(int, ^, int)->int' ) }, qr[parse signature], 'wrap() dies on invalid signature';
    like dies { Affix::sizeof('{int, double') },                         qr[parse signature], 'sizeof() dies on unterminated aggregate';
};
subtest '"Kitchen Sink" Callback' => sub {
    plan 11;
    note 'Testing a callback with 10 mixed arguments passed as a direct coderef.';
    my $cb_sig = '(*((int32, float64, int32, float64, int32, float64, int32, float64, *char, *int32)->void))->void';
    isa_ok my $harness = wrap( $lib_path, 'call_kitchen_sink', $cb_sig ), ['Affix'];
    my $callback_sub = sub {
        my ( $a, $b, $c, $d, $e, $f, $g, $h, $i, $j_ref ) = @_;
        is $a,      1,              'Callback arg 1 (int)';
        is $b,      2.0,            'Callback arg 2 (double)';
        is $c,      3,              'Callback arg 3 (int)';
        is $d,      4.0,            'Callback arg 4 (double)';
        is $e,      5,              'Callback arg 5 (int)';
        is $f,      6.0,            'Callback arg 6 (double)';
        is $g,      7,              'Callback arg 7 (int)';
        is $h,      8.0,            'Callback arg 8 (double)';
        is $i,      'kitchen sink', 'Callback arg 9 (string)';
        is $$j_ref, 100,            'Callback arg 10 (int*)';
    };
    $harness->($callback_sub);
};
subtest 'Type Registry and Typedefs' => sub {
    plan 5;
    note 'Defining named types for subsequent tests.';
    ok typedef(<<''), 'Successfully defined multiple types using typedef';
    @Point    = { x: int32, y: int32 };
    @Rect     = { top_left: @Point, bottom_right: @Point, name: *char };
    @MyStruct = { id: int32, value: float64, label: *char };
    @MyUnion  = < i:int32, f:float32, c:[8:char] >;

    subtest 'Forward Calls: Nested Structs and By-Value Returns (with Typedefs)' => sub {
        plan 3;
        isa_ok my $get_width = wrap( $lib_path, 'get_rect_width', '(*@Rect)->int32' ), ['Affix'];
        is $get_width->( \{ top_left => { x => 10, y => 20 }, bottom_right => { x => 60, y => 80 }, name => 'My Rectangle' } ), 50,
            'Correctly passed nested struct and calculated width';
        isa_ok my $create_point = wrap( $lib_path, 'create_point', '(int32, int32)->@Point' ), ['Affix'];
        my $point = $create_point->( 123, 456 );
        is $point, { x => 123, y => 456 }, 'Correctly received a struct returned by value';
    };
    subtest 'Forward Calls: Advanced Pointers and Arrays of Structs (with Typedefs)' => sub {
        plan 2;
        note 'Testing marshalling arrays of structs using typedefs.';
        isa_ok my $sum_ids = wrap( $lib_path, 'sum_struct_ids', '(*@MyStruct, int32)->int32' ), ['Affix'];
        my $struct_array
            = [ { id => 10, value => 1.1, label => 'A' }, { id => 20, value => 2.2, label => 'B' }, { id => 30, value => 3.3, label => 'C' }, ];
        is $sum_ids->( $struct_array, 3 ), 60, 'Correctly passed an array of structs and summed IDs';
    };
    subtest 'Forward Calls: Enums and Unions (with Typedefs)' => sub {
        plan 4;
        note 'Testing marshalling for enums and unions.';
        isa_ok my $check_color = wrap( $lib_path, 'check_color', '(int32)->int32' ), ['Affix'];
        is $check_color->(1), 1, 'Correctly passed an enum value (GREEN)';
        isa_ok my $process_union = wrap( $lib_path, 'process_union_float', '(@MyUnion)->float32' ), ['Affix'];
        my $union_data = { f => 2.5 };
        is $process_union->($union_data), float(25.0), 'Correctly passed a union with the float member active';
    };
    subtest 'Advanced Callbacks (Reverse FFI) (with Typedefs)' => sub {
        plan 3;
        diag 'Testing callbacks that send and receive structs by passing coderefs directly.';
        isa_ok my $harness1 = wrap( $lib_path, 'process_struct_with_cb', '(*@MyStruct, (*(@MyStruct))->float64)->float64' ), ['Affix'];
        my $struct_to_pass = { id => 100, value => 5.5, label => 'Callback Struct' };
        my $cb1            = sub ($struct_ref) {
            return $struct_ref->{value} * 2;
        };
        is $harness1->( $struct_to_pass, $cb1 ), 11.0, 'Callback coderef received struct pointer and returned correct value';
        isa_ok my $harness2 = wrap( $lib_path, 'check_returned_struct_from_cb', '( (*())->@Point )->int32' ), ['Affix'];
        is $harness2->(
            sub {
                diag "Inside callback that will return a struct";
                return { x => 70, y => 30 };
            }
            ),
            100, 'C code correctly received a struct returned by value from a Perl callback';
    };
};
done_testing;
