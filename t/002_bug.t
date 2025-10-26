use v5.40;
use blib;
use Test2::Tools::Affix qw[:all];
use Affix               qw[:all];
use Config;
#
#~ Affix::test_internal_lifecycle();
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
ok typedef(<<''), 'Successfully defined multiple types using typedef';
    @Point    = { x: int32, y: int32 };
    @Rect     = { top_left: @Point, bottom_right: @Point, name: *char };
    @MyStruct = { id: int32, value: float64, label: *char };
    @MyUnion  = < i:int32, f:float32, c:[8:char] >;

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
done_testing;
