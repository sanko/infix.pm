#include "Affix.h"
#include <string.h>

// Test: Direct Marshalling Handlers
static infix_direct_value_t affix_marshaller_sint(void * sv_raw);
static infix_direct_value_t affix_marshaller_uint(void * sv_raw);
static infix_direct_value_t affix_marshaller_double(void * sv_raw);
static infix_direct_value_t affix_marshaller_pointer(void * sv_raw);
static void affix_aggregate_marshaller(void * sv_raw, void * dest, const infix_type * type);
static void affix_aggregate_writeback(void * sv_raw, void * src, const infix_type * type);
static infix_direct_arg_handler_t get_direct_handler_for_type(const infix_type * type);

/**
 * @brief The XSUB trigger for high-performance "bundled" trampolines.
 *
 * This function is the entry point for FFI calls created with `affix_bundle`.
 * It bypasses the "execution plan" and intermediate argument buffers used by the
 * standard `Affix_trigger`.
 *
 * It performs the following steps:
 * 1.  Validates the number of arguments passed from Perl.
 * 2.  Allocates a temporary buffer on the stack for the C function's return value.
 * 3.  Calls the JIT-compiled `cif` function directly, passing it a pointer to the
 *     return buffer and a pointer to the start of the Perl stack frame (`&ST(0)`),
 *     which the JIT code treats as the `void** lang_objects_array`.
 * 4.  After the call, it invokes the pre-resolved "pull" handler to convert the
 *     C return value into a Perl SV.
 * 5.  Pushes the resulting SV onto the Perl stack as the return value.
 */
void Affix_trigger_backend(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    // 1. Initialize the TARG (Target) variable.
    // This fetches the destination SV from the optree (e.g., $result in '$result = func()')
    // or creates a lightweight temporary if needed.
    dXSTARG;

    Affix_Backend * backend = (Affix_Backend *)CvXSUBANY(cv).any_ptr;

    if (UNLIKELY((SP - MARK) != backend->num_args))
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)backend->num_args,
              (UV)(SP - MARK));

    // Allocate a buffer on the C stack for the return value. `alloca` is fast and
    // is automatically cleaned up when this function returns.
    void * ret_buffer =  // safemalloc(sizeof(double));
        alloca(infix_type_get_size(backend->ret_type));

    // Get a pointer to the start of the Perl argument stack.
    SV ** perl_stack_frame = &ST(0);

    // Call the high-performance JIT-compiled trampoline.
    backend->cif(ret_buffer, (void **)perl_stack_frame);

    // 2. Marshal C -> Perl directly into TARG.
    // TARG is already an SV* managed by Perl.
    // If it's 'my $x', we are writing directly into $x's memory. No allocation.
    backend->pull_handler(aTHX_ NULL, TARG, backend->ret_type, ret_buffer);

    // 3. Return TARG.
    // We don't need sv_2mortal because dXSTARG handles the lifecycle flags.
    ST(0) = TARG;
    PL_stack_sp = PL_stack_base + ax;  // Adjust stack pointer
}

/**
 * @brief The marshaller for all primitive integer types.
 */
static infix_direct_value_t affix_marshaller_sint(void * sv_raw) {
    dTHX;
    infix_direct_value_t val;
    val.i64 = SvIV((SV *)sv_raw);
    return val;
}

/**
 * @brief The marshaller for all primitive unsigned integer types.
 */
static infix_direct_value_t affix_marshaller_uint(void * sv_raw) {
    dTHX;
    infix_direct_value_t val;
    val.u64 = SvUV((SV *)sv_raw);
    return val;
}

/**
 * @brief The marshaller for all primitive floating-point types.
 */
static infix_direct_value_t affix_marshaller_double(void * sv_raw) {
    infix_direct_value_t val;
    SV * sv = (SV *)sv_raw;
    U32 flags = SvFLAGS(sv);  // Single memory read

    // Optimization: Float is the expected type, check it first.
    if (LIKELY(flags & SVf_NOK)) {
        val.f64 = SvNVX(sv);
    }
    // Fallback: Integer types (common for literals like '0' or '1')
    else if (flags & SVf_IOK) {
        // Check the "Is UV" bit directly from our cached flags
        if (flags & SVf_IVisUV)
            val.f64 = (double)SvUVX(sv);
        else
            val.f64 = (double)SvIVX(sv);
    }
    // Slow path: Strings ("3.14"), References, Overloaded objects
    else {
        dTHX;  // Required for SvNV fallback
        val.f64 = (double)SvNV(sv);
    }

    return val;
}
/**
 * @brief The marshaller for all pointer types.
 */
static infix_direct_value_t affix_marshaller_pointer(void * sv_raw) {
    dTHX;
    infix_direct_value_t val;
    SV * sv = (SV *)sv_raw;
    if (is_pin(aTHX_ sv)) {
        val.ptr = _get_pin_from_sv(aTHX_ sv)->pointer;
    }
    else if (SvPOK(sv)) {
        val.ptr = (void *)SvPV_nolen(sv);
    }
    else if (!SvOK(sv)) {
        val.ptr = NULL;
    }
    else {
        // A simple fallback for other pointer types (e.g., passing a blessed object).
        // A real binding would have more sophisticated logic here.
        val.ptr = INT2PTR(void *, SvIV(SvRV(sv)));
    }
    return val;
}

/**
 * @brief A generic marshaller for all aggregate types (structs/unions).
 *
 * This function uses the `infix_type*` provided by the JIT trampoline to
 * introspect the C struct's layout. It iterates through the members and
 * recursively calls `sv2ptr` to populate the C struct buffer from a Perl hash.
 */
static void affix_aggregate_marshaller(void * sv_raw, void * dest_buffer, const infix_type * type) {
    dTHX;
    SV * sv = (SV *)sv_raw;
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVHV) {
        // For simplicity, we silently do nothing if not a hashref. A real binding
        // might croak or warn here.
        return;
    }
    HV * hv = (HV *)SvRV(sv);
    for (size_t i = 0; i < infix_type_get_member_count(type); ++i) {
        const infix_struct_member * member = infix_type_get_member(type, i);
        if (member->name) {
            SV ** member_sv_ptr = hv_fetch(hv, member->name, strlen(member->name), 0);
            if (member_sv_ptr) {
                void * member_ptr = (char *)dest_buffer + member->offset;
                sv2ptr(aTHX_ NULL, *member_sv_ptr, member_ptr, member->type);
            }
        }
    }
}

/**
 * @brief A generic write-back handler for all aggregate types.
 *
 * This function is the inverse of the marshaller. It uses the `infix_type*`
 * to iterate the C struct's members and updates the fields of the original
 * Perl hash with the (potentially modified) values from the C struct.
 */
static void affix_aggregate_writeback(void * sv_raw, void * src_buffer, const infix_type * type) {
    dTHX;
    SV * sv = (SV *)sv_raw;
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVHV)
        return;

    HV * hv = (HV *)SvRV(sv);
    for (size_t i = 0; i < infix_type_get_member_count(type); ++i) {
        const infix_struct_member * member = infix_type_get_member(type, i);
        if (member->name) {
            void * member_ptr = (char *)src_buffer + member->offset;
            SV * member_sv = newSV(0);
            ptr2sv(aTHX_ NULL, member_ptr, member_sv, member->type);
            hv_store(hv, member->name, strlen(member->name), member_sv, 0);
        }
    }
}

/**
 * @brief A resolver that returns the correct set of direct handlers for a given type.
 *
 * This is the central mapping between an `infix_type` and the Perl-specific
 * marshalling functions. It is called once per argument during trampoline creation.
 */
static infix_direct_arg_handler_t get_direct_handler_for_type(const infix_type * type) {
    infix_direct_arg_handler_t h = {0};
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        if (is_float(type) || is_double(type))
            h.scalar_marshaller = &affix_marshaller_double;
        else if (type->meta.primitive_id <= INFIX_PRIMITIVE_SINT128)
            h.scalar_marshaller = &affix_marshaller_sint;
        else
            h.scalar_marshaller = &affix_marshaller_uint;
        break;
    case INFIX_TYPE_POINTER:
        {
            const infix_type * pointee = type->meta.pointer_info.pointee_type;
            if (pointee->category == INFIX_TYPE_STRUCT || pointee->category == INFIX_TYPE_UNION) {
                h.aggregate_marshaller = &affix_aggregate_marshaller;
                h.writeback_handler = &affix_aggregate_writeback;
            }
            else {
                h.scalar_marshaller = &affix_marshaller_pointer;
                // Add a basic writeback for simple pointer types like *int.
                // h.writeback_handler = ...
            }
            break;
        }
    case INFIX_TYPE_STRUCT:
    case INFIX_TYPE_UNION:
        h.aggregate_marshaller = &affix_aggregate_marshaller;
        break;
    default:
        // Other types like arrays-by-value are handled as aggregates.
        h.aggregate_marshaller = &affix_aggregate_marshaller;
        break;
    }
    return h;
}

typedef enum {
    MOCK_TYPE_INT,
    MOCK_TYPE_DOUBLE,
    MOCK_TYPE_FLOAT,
    MOCK_TYPE_POINT,
    MOCK_TYPE_STRING,
    MOCK_TYPE_LINE,
    MOCK_TYPE_FUNC
} MockObjectType;
typedef struct MockObject {
    MockObjectType type;
    union {
        int64_t i;
        double d;
        float f;
        const char * s;
        struct MockObject * fields;  // For structs, points to an array of field objects
        void * func_ptr;             // For function pointers
    } value;
} MockObject;


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//               FORWARD DECLARATIONS FOR STATIC FUNCTIONS
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Marshallers (Perl -> C)
// Note: These functions must be `extern` (not static) so they can be
// referenced by address from the language binding.
int64_t affix_perl_shim_sv_to_sint64(pTHX_ void * sv_raw) { return SvIVX((SV *)sv_raw); }
double affix_perl_shim_sv_to_double(pTHX_ void * sv_raw) { return SvNVX((SV *)sv_raw); }
const char * affix_perl_shim_sv_to_string(pTHX_ void * sv_raw) { return SvPV_nolen((SV *)sv_raw); }
void * affix_perl_shim_sv_to_pointer(pTHX_ void * sv_raw) {
    SV * sv = (SV *)sv_raw;
    if (!SvOK(sv) || !SvROK(sv))
        return NULL;
    // A proper implementation would check for Affix::Pin objects here.
    // For now, we assume it's a raw pointer stored in the IV slot.
    return INT2PTR(void *, SvIV(SvRV(sv)));
}

// Unmarshallers (C -> Perl) - Not used in this forward-call implementation
void * affix_perl_shim_newSViv(pTHX_ int64_t value) { return newSViv(value); }
void * affix_perl_shim_newSVnv(pTHX_ double value) { return newSVnv(value); }
void * affix_perl_shim_newSVpv(pTHX_ const char * value) { return newSVpv(value, 0); }

// Pin Management
static int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg);
static int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg);
static U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg);
static int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg);

// Execution Plan Step Executors
static void plan_step_push_bool(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_sint8(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_uint8(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_sint16(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_uint16(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_sint32(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_uint32(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_sint64(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_uint64(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_float(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_double(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_long_double(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
#if !defined(INFIX_COMPILER_MSVC)
static void plan_step_push_sint128(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_uint128(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
#endif
static void plan_step_push_pointer(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_struct(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_union(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_array(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_enum(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_complex(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_vector(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_sv(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_push_callback(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_call_c_function(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);
static void plan_step_pull_return_value(pTHX_ Affix *, Affix_Plan_Step *, SV **, void *, void **, void *);

// sv2ptr Primitive Push Handlers
static void push_handler_bool(pTHX_ Affix *, SV *, void *);
static void push_handler_sint8(pTHX_ Affix *, SV *, void *);
static void push_handler_uint8(pTHX_ Affix *, SV *, void *);
static void push_handler_sint16(pTHX_ Affix *, SV *, void *);
static void push_handler_uint16(pTHX_ Affix *, SV *, void *);
static void push_handler_sint32(pTHX_ Affix *, SV *, void *);
static void push_handler_uint32(pTHX_ Affix *, SV *, void *);
static void push_handler_sint64(pTHX_ Affix *, SV *, void *);
static void push_handler_uint64(pTHX_ Affix *, SV *, void *);
static void push_handler_float(pTHX_ Affix *, SV *, void *);
static void push_handler_double(pTHX_ Affix *, SV *, void *);
static void push_handler_long_double(pTHX_ Affix *, SV *, void *);

// Pull handlers (C -> Perl)
static void pull_sint8(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_uint8(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_sint16(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_uint16(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_sint32(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_uint32(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_sint64(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_uint64(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_float(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_double(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_long_double(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_bool(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_void(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_struct(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_union(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_array(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_enum(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_complex(pTHX_ Affix *, SV *, const infix_type *, void * p);
static void pull_vector(pTHX_ Affix *, SV *, const infix_type *, void * p);
static void pull_pointer(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_pointer_as_string(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_pointer_as_struct(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_pointer_as_array(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_pointer_as_pin(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_sv(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_reverse_trampoline(pTHX_ Affix *, SV *, const infix_type *, void *);
#if !defined(INFIX_COMPILER_MSVC)
static void pull_sint128(pTHX_ Affix *, SV *, const infix_type *, void *);
static void pull_uint128(pTHX_ Affix *, SV *, const infix_type *, void *);
#endif


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//              MACROS AND DEFINITIONS (BEFORE STATIC DATA)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define DEFINE_PUSH_PRIMITIVE_EXECUTOR(name, c_type, sv_accessor)         \
    static void plan_step_push_##name(pTHX_ Affix * affix,                \
                                      Affix_Plan_Step * step,             \
                                      SV ** perl_stack_frame,             \
                                      void * args_buffer,                 \
                                      void ** c_args,                     \
                                      void * ret_buffer) {                \
        PERL_UNUSED_VAR(affix);                                           \
        PERL_UNUSED_VAR(ret_buffer);                                      \
        SV * sv = perl_stack_frame[step->data.index];                     \
        void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset; \
        *(c_type *)c_arg_ptr = (c_type)sv_accessor(sv);                   \
        c_args[step->data.index] = c_arg_ptr;                             \
    }

#define DEFINE_PUSH_HANDLER(name, c_type, ok_checker, sv_accessor)                     \
    static void push_handler_##name(pTHX_ Affix * affix, SV * perl_sv, void * c_ptr) { \
        PERL_UNUSED_VAR(affix);                                                        \
        *(c_type *)c_ptr = (c_type)(ok_checker(perl_sv) ? sv_accessor(perl_sv) : 0);   \
    }

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//              FUNCTION DEFINITIONS (BEFORE STATIC DATA)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// In Affix.c - Helper to map type to opcode
static Affix_Opcode get_opcode_for_type(const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        switch (type->meta.primitive_id) {
        case INFIX_PRIMITIVE_BOOL:
            return OP_PUSH_BOOL;
        case INFIX_PRIMITIVE_SINT8:
            return OP_PUSH_SINT8;
        case INFIX_PRIMITIVE_UINT8:
            return OP_PUSH_UINT8;
        case INFIX_PRIMITIVE_SINT16:
            return OP_PUSH_SINT16;
        case INFIX_PRIMITIVE_UINT16:
            return OP_PUSH_UINT16;
        case INFIX_PRIMITIVE_SINT32:
            return OP_PUSH_SINT32;
        case INFIX_PRIMITIVE_UINT32:
            return OP_PUSH_UINT32;
        case INFIX_PRIMITIVE_SINT64:
            return OP_PUSH_SINT64;
        case INFIX_PRIMITIVE_UINT64:
            return OP_PUSH_UINT64;
        case INFIX_PRIMITIVE_FLOAT:
            return OP_PUSH_FLOAT;
        case INFIX_PRIMITIVE_DOUBLE:
            return OP_PUSH_DOUBLE;
        default:
            return OP_PUSH_DOUBLE;  // Fallback/LongDouble
        }
    case INFIX_TYPE_POINTER:
        {
            const char * name = infix_type_get_name(type);
            if (name && strnEQ(name, "SV", 2))
                return OP_PUSH_SV;
            return OP_PUSH_POINTER;
        }
    case INFIX_TYPE_STRUCT:
        return OP_PUSH_STRUCT;
    case INFIX_TYPE_UNION:
        return OP_PUSH_UNION;
    case INFIX_TYPE_ARRAY:
        return OP_PUSH_ARRAY;
    case INFIX_TYPE_ENUM:
        return OP_PUSH_ENUM;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        return OP_PUSH_CALLBACK;
    // ... others
    default:
        return OP_PUSH_STRUCT;  // Fallback
    }
}
// --- Define sv2ptr primitive handlers ---
DEFINE_PUSH_HANDLER(sint8, int8_t, SvIOK, SvIV)
DEFINE_PUSH_HANDLER(uint8, uint8_t, SvUOK, SvUV)
DEFINE_PUSH_HANDLER(sint16, int16_t, SvIOK, SvIV)
DEFINE_PUSH_HANDLER(uint16, uint16_t, SvUOK, SvUV)
DEFINE_PUSH_HANDLER(sint32, int32_t, SvIOK, SvIV)
DEFINE_PUSH_HANDLER(uint32, uint32_t, SvUOK, SvUV)
DEFINE_PUSH_HANDLER(sint64, int64_t, SvIOK, SvIV)
DEFINE_PUSH_HANDLER(uint64, uint64_t, SvUOK, SvUV)
DEFINE_PUSH_HANDLER(float, float, SvNOK, SvNV)
// DEFINE_PUSH_HANDLER(double, double, SvNOK, SvNV)

static void push_handler_double(pTHX_ Affix * affix, SV * sv, void * c_ptr) {
    PERL_UNUSED_VAR(affix);
    U32 flags = SvFLAGS(sv);  // Single memory read

    // Optimization: Float is the expected type, check it first.
    if (LIKELY(flags & SVf_NOK))
        *(double *)c_ptr = SvNVX(sv);
    // Fallback: Integer types (common for literals like '0' or '1')
    else if (flags & SVf_IOK) {
        // Check the "Is UV" bit directly from our cached flags
        if (flags & SVf_IVisUV)
            *(double *)c_ptr = (double)SvUVX(sv);
        else
            *(double *)c_ptr = (double)SvIVX(sv);
    }
    // Slow path: Strings ("3.14"), References, Overloaded objects
    else {
        dTHX;  // Required for SvNV fallback
        *(double *)c_ptr = (double)SvNV(sv);
    }

    return;
}

DEFINE_PUSH_HANDLER(long_double, long double, SvNOK, SvNV)

static void push_handler_bool(pTHX_ Affix * affix, SV * perl_sv, void * c_ptr) {
    PERL_UNUSED_VAR(affix);
    *(bool *)c_ptr = SvTRUE(perl_sv);
}

// --- Define primitive push executors ---
DEFINE_PUSH_PRIMITIVE_EXECUTOR(bool, bool, SvTRUE)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(sint8, int8_t, SvIV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(uint8, uint8_t, SvUV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(sint16, int16_t, SvIV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(uint16, uint16_t, SvUV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(sint32, int32_t, SvIV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(uint32, uint32_t, SvUV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(sint64, int64_t, SvIV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(uint64, uint64_t, SvUV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(float, float, SvNV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(double, double, SvNV)
DEFINE_PUSH_PRIMITIVE_EXECUTOR(long_double, long double, SvNV)

#if !defined(INFIX_COMPILER_MSVC)
static void plan_step_push_sint128(pTHX_ Affix * affix,
                                   Affix_Plan_Step * step,
                                   SV ** perl_stack_frame,
                                   void * args_buffer,
                                   void ** c_args,
                                   void * ret_buffer) {
    croak("128-bit integer marshalling not yet implemented");
}
static void plan_step_push_uint128(pTHX_ Affix * affix,
                                   Affix_Plan_Step * step,
                                   SV ** perl_stack_frame,
                                   void * args_buffer,
                                   void ** c_args,
                                   void * ret_buffer) {
    croak("128-bit integer marshalling not yet implemented");
}
#endif

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//                      STATIC DATA & DEFINITIONS
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static MGVTBL Affix_pin_vtbl = {Affix_get_pin, Affix_set_pin, Affix_len_pin, NULL, Affix_free_pin, NULL, NULL, NULL};

static const Affix_Step_Executor primitive_executors[] = {
    [INFIX_PRIMITIVE_BOOL] = plan_step_push_bool,
    [INFIX_PRIMITIVE_SINT8] = plan_step_push_sint8,
    [INFIX_PRIMITIVE_UINT8] = plan_step_push_uint8,
    [INFIX_PRIMITIVE_SINT16] = plan_step_push_sint16,
    [INFIX_PRIMITIVE_UINT16] = plan_step_push_uint16,
    [INFIX_PRIMITIVE_SINT32] = plan_step_push_sint32,
    [INFIX_PRIMITIVE_UINT32] = plan_step_push_uint32,
    [INFIX_PRIMITIVE_SINT64] = plan_step_push_sint64,
    [INFIX_PRIMITIVE_UINT64] = plan_step_push_uint64,
    [INFIX_PRIMITIVE_FLOAT] = plan_step_push_float,
    [INFIX_PRIMITIVE_DOUBLE] = plan_step_push_double,
    [INFIX_PRIMITIVE_LONG_DOUBLE] = plan_step_push_long_double,
#if !defined(INFIX_COMPILER_MSVC)
    [INFIX_PRIMITIVE_SINT128] = plan_step_push_sint128,
    [INFIX_PRIMITIVE_UINT128] = plan_step_push_uint128,
#endif
};

static const Affix_Push_Handler primitive_push_handlers[] = {
    [INFIX_PRIMITIVE_BOOL] = push_handler_bool,
    [INFIX_PRIMITIVE_SINT8] = push_handler_sint8,
    [INFIX_PRIMITIVE_UINT8] = push_handler_uint8,
    [INFIX_PRIMITIVE_SINT16] = push_handler_sint16,
    [INFIX_PRIMITIVE_UINT16] = push_handler_uint16,
    [INFIX_PRIMITIVE_SINT32] = push_handler_sint32,
    [INFIX_PRIMITIVE_UINT32] = push_handler_uint32,
    [INFIX_PRIMITIVE_SINT64] = push_handler_sint64,
    [INFIX_PRIMITIVE_UINT64] = push_handler_uint64,
    [INFIX_PRIMITIVE_FLOAT] = push_handler_float,
    [INFIX_PRIMITIVE_DOUBLE] = push_handler_double,
    [INFIX_PRIMITIVE_LONG_DOUBLE] = push_handler_long_double,
};

// Signature helper
static const char * _get_string_from_type_obj(pTHX_ SV * type_sv) {
    if (sv_isobject(type_sv) && sv_derived_from(type_sv, "Affix::Type")) {
        if (SvROK(type_sv)) {
            SV * rv = SvRV(type_sv);
            if (SvTYPE(rv) == SVt_PVHV) {
                HV * hv = (HV *)rv;
                SV ** stringify_sv_ptr = hv_fetchs(hv, "stringify", 0);
                if (stringify_sv_ptr && SvPOK(*stringify_sv_ptr))
                    return SvPV_nolen(*stringify_sv_ptr);
            }
        }
    }
    return SvPV_nolen(type_sv);
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//                      COMPLEX TYPE STEP EXECUTORS
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static void plan_step_push_pointer(pTHX_ Affix * affix,
                                   Affix_Plan_Step * step,
                                   SV ** perl_stack_frame,
                                   void * args_buffer,
                                   void ** c_args,
                                   void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;

    if (is_pin(aTHX_ sv)) {
        *(void **)c_arg_ptr = _get_pin_from_sv(aTHX_ sv)->pointer;
        return;
    }
    const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
    if (pointee_type == NULL)
        croak("Internal error in push_pointer: pointee_type is NULL");
    if (!SvOK(sv)) {
        *(void **)c_arg_ptr = NULL;
        return;
    }
    // Check if we are passing a coderef for a function pointer argument.
    // A coderef can be passed directly, not just as a reference.
    if (pointee_type->category == INFIX_TYPE_REVERSE_TRAMPOLINE &&
        (SvTYPE(sv) == SVt_PVCV || (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVCV))) {
        push_reverse_trampoline(aTHX_ affix, pointee_type, sv, c_arg_ptr);
        return;
    }
    if (SvROK(sv)) {
        SV * const rv = SvRV(sv);
        // Special case: **char for modifiable string pointers
        if (pointee_type->category == INFIX_TYPE_POINTER) {
            const infix_type * inner_pointee_type = pointee_type->meta.pointer_info.pointee_type;
            if (inner_pointee_type->category == INFIX_TYPE_PRIMITIVE &&
                (inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
                 inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
                if (SvPOK(rv)) {
                    char ** ptr_slot = (char **)infix_arena_alloc(affix->args_arena, sizeof(char *), _Alignof(char *));
                    *ptr_slot = SvPV_nolen(rv);
                    *(void **)c_arg_ptr = ptr_slot;
                    return;
                }
            }
        }
        if (SvTYPE(rv) == SVt_PVAV) {
            AV * av = (AV *)rv;
            size_t len = av_len(av) + 1;
            size_t element_size = infix_type_get_size(pointee_type);
            size_t total_size = len * element_size;
            char * c_array = (char *)infix_arena_alloc(affix->args_arena, total_size, _Alignof(void *));
            if (!c_array)
                croak("Failed to allocate from arena for array marshalling");
            memset(c_array, 0, total_size);
            for (size_t i = 0; i < len; ++i) {
                SV ** elem_sv_ptr = av_fetch(av, i, 0);
                if (elem_sv_ptr)
                    sv2ptr(aTHX_ affix, *elem_sv_ptr, c_array + (i * element_size), pointee_type);
            }
            *(void **)c_arg_ptr = c_array;
            return;
        }
        const infix_type * copy_type = (pointee_type->category == INFIX_TYPE_VOID)
            ? (SvIOK(rv)       ? infix_type_create_primitive(INFIX_PRIMITIVE_SINT64)
                   : SvNOK(rv) ? infix_type_create_primitive(INFIX_PRIMITIVE_DOUBLE)
                   : SvPOK(rv) ? (*(void **)c_arg_ptr = SvPV_nolen(rv), (infix_type *)NULL)
                               : (croak("Cannot pass reference to this type of scalar for a 'void*' parameter"),
                                  (infix_type *)NULL))
            : pointee_type;
        if (!copy_type)
            return;
        void * dest_c_ptr =
            infix_arena_alloc(affix->args_arena, infix_type_get_size(copy_type), infix_type_get_alignment(copy_type));
        SV * sv_to_marshal = (SvTYPE(rv) == SVt_PVHV) ? sv : rv;
        sv2ptr(aTHX_ affix, sv_to_marshal, dest_c_ptr, copy_type);
        *(void **)c_arg_ptr = dest_c_ptr;
        return;
    }
    if (SvPOK(sv) && pointee_type->category == INFIX_TYPE_PRIMITIVE &&
        (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
        *(const char **)c_arg_ptr = SvPV_nolen(sv);
        return;
    }
    PING;
    sv_dump(sv);
    char signature_buf[256];
    (void)infix_type_print(signature_buf, sizeof(signature_buf), (infix_type *)type, INFIX_DIALECT_SIGNATURE);
    croak("Don't know how to handle this type of scalar as a pointer argument yet: %s", signature_buf);
}
static void plan_step_push_struct(pTHX_ Affix * affix,
                                  Affix_Plan_Step * step,
                                  SV ** perl_stack_frame,
                                  void * args_buffer,
                                  void ** c_args,
                                  void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    push_struct(aTHX_ affix, type, sv, c_arg_ptr);
}
static void plan_step_push_union(pTHX_ Affix * affix,
                                 Affix_Plan_Step * step,
                                 SV ** perl_stack_frame,
                                 void * args_buffer,
                                 void ** c_args,
                                 void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVHV)
        croak("Expected a HASH reference for union marshalling");
    HV * hv = (HV *)SvRV(sv);
    if (hv_iterinit(hv) == 0)
        return;
    HE * he = hv_iternext(hv);
    if (!he)
        return;
    const char * key = HeKEY(he);
    STRLEN key_len = HeKLEN(he);
    SV * value_sv = HeVAL(he);
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (member->name && strlen(member->name) == key_len && memcmp(member->name, key, key_len) == 0) {
            sv2ptr(aTHX_ affix, value_sv, c_arg_ptr, member->type);
            return;
        }
    }
    croak("Union member '%s' not found in type definition", key);
}
static void plan_step_push_array(pTHX_ Affix * affix,
                                 Affix_Plan_Step * step,
                                 SV ** perl_stack_frame,
                                 void * args_buffer,
                                 void ** c_args,
                                 void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * temp_buffer = (char *)args_buffer + step->data.c_arg_offset;
    push_array(aTHX_ affix, type, sv, temp_buffer);
    c_args[step->data.index] = temp_buffer;
}
static void plan_step_push_enum(pTHX_ Affix * affix,
                                Affix_Plan_Step * step,
                                SV ** perl_stack_frame,
                                void * args_buffer,
                                void ** c_args,
                                void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    sv2ptr(aTHX_ affix, sv, c_arg_ptr, type->meta.enum_info.underlying_type);
}
static void plan_step_push_complex(pTHX_ Affix * affix,
                                   Affix_Plan_Step * step,
                                   SV ** perl_stack_frame,
                                   void * args_buffer,
                                   void ** c_args,
                                   void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference with two numbers for complex type marshalling");
    AV * av = (AV *)SvRV(sv);
    if (av_len(av) != 1)
        croak("Expected exactly two elements (real, imaginary) for complex type");
    const infix_type * base_type = type->meta.complex_info.base_type;
    size_t base_size = infix_type_get_size(base_type);
    SV ** real_sv_ptr = av_fetch(av, 0, 0);
    SV ** imag_sv_ptr = av_fetch(av, 1, 0);
    if (!real_sv_ptr || !imag_sv_ptr)
        croak("Failed to fetch real or imaginary part from array for complex type");
    sv2ptr(aTHX_ affix, *real_sv_ptr, c_arg_ptr, base_type);
    sv2ptr(aTHX_ affix, *imag_sv_ptr, (char *)c_arg_ptr + base_size, base_type);
}
static void plan_step_push_vector(pTHX_ Affix * affix,
                                  Affix_Plan_Step * step,
                                  SV ** perl_stack_frame,
                                  void * args_buffer,
                                  void ** c_args,
                                  void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference for vector marshalling");
    AV * av = (AV *)SvRV(sv);
    size_t num_elements = av_len(av) + 1;
    size_t c_vector_len = type->meta.vector_info.num_elements;
    if (num_elements != c_vector_len)
        croak("Perl array has %lu elements, but C vector type requires %lu.",
              (unsigned long)num_elements,
              (unsigned long)c_vector_len);
    const infix_type * element_type = type->meta.vector_info.element_type;
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_elements; ++i) {
        SV ** element_sv_ptr = av_fetch(av, i, 0);
        if (element_sv_ptr) {
            void * element_ptr = (char *)c_arg_ptr + (i * element_size);
            sv2ptr(aTHX_ affix, *element_sv_ptr, element_ptr, element_type);
        }
    }
}
static void plan_step_push_sv(pTHX_ Affix * affix,
                              Affix_Plan_Step * step,
                              SV ** perl_stack_frame,
                              void * args_buffer,
                              void ** c_args,
                              void * ret_buffer) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(ret_buffer);
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    SvREFCNT_inc(sv);
    *(void **)c_arg_ptr = sv;
}
static void plan_step_push_callback(pTHX_ Affix * affix,
                                    Affix_Plan_Step * step,
                                    SV ** perl_stack_frame,
                                    void * args_buffer,
                                    void ** c_args,
                                    void * ret_buffer) {
    PERL_UNUSED_VAR(ret_buffer);
    const infix_type * type = step->data.type;
    SV * sv = perl_stack_frame[step->data.index];
    void * c_arg_ptr = (char *)args_buffer + step->data.c_arg_offset;
    c_args[step->data.index] = c_arg_ptr;
    push_reverse_trampoline(aTHX_ affix, type, sv, c_arg_ptr);
}
static void plan_step_call_c_function(pTHX_ Affix * affix,
                                      Affix_Plan_Step * step,
                                      SV ** perl_stack_frame,
                                      void * args_buffer,
                                      void ** c_args,
                                      void * ret_buffer) {
    PERL_UNUSED_VAR(step);
    PERL_UNUSED_VAR(perl_stack_frame);
    PERL_UNUSED_VAR(args_buffer);
    affix->cif(ret_buffer, c_args);
}
static void plan_step_pull_return_value(pTHX_ Affix * affix,
                                        Affix_Plan_Step * step,
                                        SV ** perl_stack_frame,
                                        void * args_buffer,
                                        void ** c_args,
                                        void * ret_buffer) {
    PERL_UNUSED_VAR(perl_stack_frame);
    PERL_UNUSED_VAR(args_buffer);
    PERL_UNUSED_VAR(c_args);
    step->data.pull_handler(aTHX_ affix, affix->return_sv, step->data.type, ret_buffer);
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//                      HANDLER RESOLUTION
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Affix_Step_Executor get_plan_step_executor(const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        // Return the pre-resolved function pointer for the specific primitive type.
        return primitive_executors[type->meta.primitive_id];
    case INFIX_TYPE_POINTER:
        {
            const char * name = infix_type_get_name(type);
            if (name && strnEQ(name, "SV", 2))
                return plan_step_push_sv;
            return plan_step_push_pointer;
        }
    case INFIX_TYPE_STRUCT:
        return plan_step_push_struct;
    case INFIX_TYPE_UNION:
        return plan_step_push_union;
    case INFIX_TYPE_ARRAY:
        return plan_step_push_array;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        return plan_step_push_callback;
    case INFIX_TYPE_ENUM:
        return plan_step_push_enum;
    case INFIX_TYPE_COMPLEX:
        return plan_step_push_complex;
    case INFIX_TYPE_VECTOR:
        return plan_step_push_vector;
    default:
        return NULL;
    }
}
static void writeback_primitive(pTHX_ Affix * affix, const OutParamInfo * info, SV * perl_sv, void * c_arg_ptr) {
    void * actual_data_ptr = *(void **)c_arg_ptr;
    ptr2sv(aTHX_ affix, actual_data_ptr, perl_sv, info->pointee_type);
}
static void writeback_struct(pTHX_ Affix * affix, const OutParamInfo * info, SV * perl_sv, void * c_arg_ptr) {
    if (SvTYPE(perl_sv) == SVt_PVHV) {
        void * struct_ptr = *(void **)c_arg_ptr;
        _populate_hv_from_c_struct(aTHX_ affix, (HV *)perl_sv, info->pointee_type, struct_ptr);
    }
}
static void writeback_pointer_to_string(pTHX_ Affix * affix,
                                        const OutParamInfo * info,
                                        SV * perl_sv,
                                        void * c_arg_ptr) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(info);
    sv_setpv(perl_sv, **(char ***)c_arg_ptr);
}
static void writeback_pointer_generic(pTHX_ Affix * affix, const OutParamInfo * info, SV * perl_sv, void * c_arg_ptr) {
    if (SvROK(perl_sv)) {
        void * inner_ptr = *(void **)c_arg_ptr;
        ptr2sv(aTHX_ affix, inner_ptr, SvRV(perl_sv), info->pointee_type->meta.pointer_info.pointee_type);
    }
}
Affix_Out_Param_Writer get_out_param_writer(const infix_type * pointee_type) {
    if (pointee_type->category == INFIX_TYPE_STRUCT)
        return writeback_struct;
    if (pointee_type->category == INFIX_TYPE_POINTER) {
        const infix_type * inner_pointee_type = pointee_type->meta.pointer_info.pointee_type;
        if (inner_pointee_type->category == INFIX_TYPE_PRIMITIVE &&
            (inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
             inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
            return writeback_pointer_to_string;
        }
        return writeback_pointer_generic;
    }
    return writeback_primitive;
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//                      MAIN TRIGGER & XS FUNCTIONS
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void xAffix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    // OPTIMIZATION: Initialize TARG directly. This provides a target SV
    // (either from the pad or a fresh mortal) to write the return value into,
    // avoiding intermediate copies.
    dXSTARG;

    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;

    if (UNLIKELY((SP - MARK) != affix->num_args))
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)affix->num_args,
              (UV)(SP - MARK));

    SV ** perl_stack_frame = &ST(0);

    // OPTIMIZATION: Stack Allocation
    // Use alloca for argument buffers if they fit within a safe threshold (2KB).
    // This eliminates the overhead of infix_arena_reset/alloc for the vast majority of calls.
    void * args_buffer;
    void ** c_args;
    void * ret_buffer;

    // 2048 bytes covers most typical C function signatures (e.g. 256 doubles).
    if (LIKELY(affix->total_args_size < 2048)) {
        args_buffer = alloca(affix->total_args_size);
        c_args = (void **)alloca(affix->num_args * sizeof(void *));
    }
    else {
        // Fallback to Arena for large structures
        affix->args_arena->current_offset = 0;
        args_buffer = affix->args_arena->buffer;
        affix->args_arena->current_offset += affix->total_args_size;
        c_args = affix->c_args;
    }

    // Separate check for return buffer size
    if (LIKELY(affix->ret_type->size < 256)) {
        ret_buffer = alloca(affix->ret_type->size);
    }
    else {
        affix->ret_arena->current_offset = 0;
        ret_buffer = affix->ret_arena->buffer;
        affix->ret_arena->current_offset += affix->ret_type->size;
    }

    // Main Execution Loop
    // Iterate only over the arguments. The call and return steps are now handled explicitly.
    // This removes indirect calls and branch misprediction overhead inside the loop.
    for (size_t i = 0; i < affix->num_args; ++i)
        affix->plan[i].executor(aTHX_ affix, &affix->plan[i], perl_stack_frame, args_buffer, c_args, ret_buffer);

    // Call C function
    affix->cif(ret_buffer, c_args);

    // OPTIMIZATION: Inline Return Handling
    // Call the pre-resolved pull handler directly into TARG.
    if (affix->ret_pull_handler)
        affix->ret_pull_handler(aTHX_ affix, TARG, affix->ret_type, ret_buffer);

    // Out-Parameter Write-Back
    // Optimized filtering loop using stack allocation for the index list.
    if (affix->num_out_params > 0) {
        size_t valid_out_indices[affix->num_out_params];
        size_t num_valid_out_params = 0;

        for (size_t i = 0; i < affix->num_out_params; ++i) {
            const OutParamInfo * info = &affix->out_param_info[i];
            SV * arg_sv = perl_stack_frame[info->perl_stack_index];

            if (SvROK(arg_sv) && !is_pin(aTHX_ arg_sv))
                valid_out_indices[num_valid_out_params++] = i;
        }

        for (size_t i = 0; i < num_valid_out_params; ++i) {
            size_t info_idx = valid_out_indices[i];
            const OutParamInfo * info = &affix->out_param_info[info_idx];

            SV * rsv = SvRV(perl_stack_frame[info->perl_stack_index]);

            if (SvTYPE(rsv) == SVt_PVAV)
                continue;

            // Use the local c_args pointer, which might be on the stack
            info->writer(aTHX_ affix, info, rsv, c_args[info->perl_stack_index]);
        }
    }

    // Return TARG
    ST(0) = TARG;
    PL_stack_sp = PL_stack_base + ax;
    return;
}

// Dispatcher Macros
#if defined(__GNUC__) || defined(__clang__)
#define USE_COMPUTED_GOTO 1
#define DISPATCH_TABLE                                                                                  \
    static void * dispatch_table[] = {                                                                  \
        &&CASE_OP_PUSH_BOOL,     &&CASE_OP_PUSH_SINT8,  &&CASE_OP_PUSH_UINT8,   &&CASE_OP_PUSH_SINT16,  \
        &&CASE_OP_PUSH_UINT16,   &&CASE_OP_PUSH_SINT32, &&CASE_OP_PUSH_UINT32,  &&CASE_OP_PUSH_SINT64,  \
        &&CASE_OP_PUSH_UINT64,   &&CASE_OP_PUSH_FLOAT,  &&CASE_OP_PUSH_DOUBLE,  &&CASE_OP_PUSH_POINTER, \
        &&CASE_OP_PUSH_SV,       &&CASE_OP_PUSH_STRUCT, &&CASE_OP_PUSH_UNION,   &&CASE_OP_PUSH_ARRAY,   \
        &&CASE_OP_PUSH_CALLBACK, &&CASE_OP_PUSH_ENUM,   &&CASE_OP_PUSH_COMPLEX, &&CASE_OP_PUSH_VECTOR};
#define DISPATCH()                               \
    do {                                         \
        step++;                                  \
        if (step < end)                          \
            goto * dispatch_table[step->opcode]; \
        else                                     \
            goto DONE;                           \
    } while (0)
#define TARGET(op) CASE_##op:
#else
#define USE_COMPUTED_GOTO 0
#define DISPATCH_TABLE
#define DISPATCH() goto DISPATCH_LOOP
#define TARGET(op) case op:
#endif

void Affix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    dXSTARG;
    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;

    if (UNLIKELY((SP - MARK) != affix->num_args))
        croak("Wrong number of arguments. Expected %d, got %d", (int)affix->num_args, (int)(SP - MARK));

    SV ** perl_stack_frame = &ST(0);

    // Stack allocation logic (same as before)
    void * args_buffer;
    void ** c_args;
    void * ret_buffer;

    if (LIKELY(affix->total_args_size < 2048)) {
        args_buffer = alloca(affix->total_args_size);
        c_args = (void **)alloca(affix->num_args * sizeof(void *));
    }
    else {
        affix->args_arena->current_offset = 0;
        args_buffer = affix->args_arena->buffer;
        affix->args_arena->current_offset += affix->total_args_size;
        c_args = affix->c_args;
    }

    if (LIKELY(affix->ret_type->size < 256)) {
        ret_buffer = alloca(affix->ret_type->size);
    }
    else {
        affix->ret_arena->current_offset = 0;
        ret_buffer = affix->ret_arena->buffer;
        affix->ret_arena->current_offset += affix->ret_type->size;
    }

    // --- THE VM LOOP ---
    Affix_Plan_Step * step = affix->plan;
    Affix_Plan_Step * end = step + affix->num_args;

    // Declare table for GCC
    DISPATCH_TABLE;

    // Initial jump
    if (step < end) {
#if USE_COMPUTED_GOTO
        goto * dispatch_table[step->opcode];
#else
DISPATCH_LOOP:
        switch (step->opcode) {
#endif
    }
    else {
        goto DONE;
    }

    // --- INSTRUCTIONS ---

    TARGET(OP_PUSH_SINT32) {
        SV * sv = perl_stack_frame[step->data.index];
        void * ptr = (char *)args_buffer + step->data.c_arg_offset;
        *(int32_t *)ptr = (int32_t)SvIV(sv);
        c_args[step->data.index] = ptr;
        DISPATCH();
    }

    TARGET(OP_PUSH_UINT32) {
        SV * sv = perl_stack_frame[step->data.index];
        void * ptr = (char *)args_buffer + step->data.c_arg_offset;
        *(uint32_t *)ptr = (uint32_t)SvUV(sv);
        c_args[step->data.index] = ptr;
        DISPATCH();
    }

    TARGET(OP_PUSH_SINT64) {
        SV * sv = perl_stack_frame[step->data.index];
        void * ptr = (char *)args_buffer + step->data.c_arg_offset;
        *(int64_t *)ptr = (int64_t)SvIV(sv);
        c_args[step->data.index] = ptr;
        DISPATCH();
    }

    TARGET(OP_PUSH_DOUBLE) {
        SV * sv = perl_stack_frame[step->data.index];
        void * ptr = (char *)args_buffer + step->data.c_arg_offset;
        // Inline the optimized double check logic here for speed
        U32 flags = SvFLAGS(sv);
        if (LIKELY(flags & SVf_NOK))
            *(double *)ptr = SvNVX(sv);
        else if (flags & SVf_IOK)
            *(double *)ptr = (double)((flags & SVf_IVisUV) ? SvUVX(sv) : SvIVX(sv));
        else
            *(double *)ptr = (double)SvNV(sv);

        c_args[step->data.index] = ptr;
        DISPATCH();
    }

    TARGET(OP_PUSH_POINTER) {
        // Inline the pointer logic, or call a static inline helper
        SV * sv = perl_stack_frame[step->data.index];
        void * ptr = (char *)args_buffer + step->data.c_arg_offset;
        c_args[step->data.index] = ptr;

        if (is_pin(aTHX_ sv)) {
            *(void **)ptr = _get_pin_from_sv(aTHX_ sv)->pointer;
        }
        else if (SvPOK(sv)) {
            *(void **)ptr = (void *)SvPV_nolen(sv);
        }
        else if (!SvOK(sv)) {
            *(void **)ptr = NULL;
        }
        else {
            // Fallback to complex logic for arrays/structs passed as ptrs
            // We can keep the old function for complex cases to keep the VM small
            step->executor(aTHX_ affix, step, perl_stack_frame, args_buffer, c_args, ret_buffer);
        }
        DISPATCH();
    }

    TARGET(OP_PUSH_STRUCT)
    TARGET(OP_PUSH_UNION)
    TARGET(OP_PUSH_ARRAY)
    TARGET(OP_PUSH_CALLBACK)
    TARGET(OP_PUSH_ENUM)
    TARGET(OP_PUSH_COMPLEX)
    TARGET(OP_PUSH_VECTOR)
    TARGET(OP_PUSH_SV) {
        // For complex types, falling back to the function pointer is acceptable
        // as the marshalling overhead dominates the dispatch overhead.
        step->executor(aTHX_ affix, step, perl_stack_frame, args_buffer, c_args, ret_buffer);
        DISPATCH();
    }

    // Primitives not explicitly optimized above can fall through or be added
    TARGET(OP_PUSH_BOOL)
    TARGET(OP_PUSH_SINT8)
    TARGET(OP_PUSH_UINT8)
    TARGET(OP_PUSH_SINT16)
    TARGET(OP_PUSH_UINT16)
    TARGET(OP_PUSH_UINT64)
    TARGET(OP_PUSH_FLOAT) {
        step->executor(aTHX_ affix, step, perl_stack_frame, args_buffer, c_args, ret_buffer);
        DISPATCH();
    }

#if !USE_COMPUTED_GOTO
}  // End switch
#endif

DONE:

    // --- CALL ---
    affix->cif(ret_buffer, c_args);

// --- RETURN ---
if (affix->ret_pull_handler)
    affix->ret_pull_handler(aTHX_ affix, TARG, affix->ret_type, ret_buffer);

// --- WRITEBACK (Out params) ---
// (Keep existing writeback logic)
if (affix->num_out_params > 0) {
    size_t valid_out_indices[affix->num_out_params];
    size_t num_valid_out_params = 0;

    for (size_t i = 0; i < affix->num_out_params; ++i) {
        const OutParamInfo * info = &affix->out_param_info[i];
        SV * arg_sv = perl_stack_frame[info->perl_stack_index];

        if (SvROK(arg_sv) && !is_pin(aTHX_ arg_sv))
            valid_out_indices[num_valid_out_params++] = i;
    }

    for (size_t i = 0; i < num_valid_out_params; ++i) {
        size_t info_idx = valid_out_indices[i];
        const OutParamInfo * info = &affix->out_param_info[info_idx];

        SV * rsv = SvRV(perl_stack_frame[info->perl_stack_index]);

        if (SvTYPE(rsv) == SVt_PVAV)
            continue;

        // Use the local c_args pointer, which might be on the stack
        info->writer(aTHX_ affix, info, rsv, c_args[info->perl_stack_index]);
    }
}

ST(0) = TARG;
PL_stack_sp = PL_stack_base + ax;
}


void xxxAffix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;
    // if (!affix)
    //     croak("Internal error: Affix context is NULL in trigger");

    if ((SP - MARK) != affix->num_args)
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)affix->num_args,
              (UV)(SP - MARK));

    SV ** perl_stack_frame = &ST(0);

    // Reset arenas (fast pointer reset)
    affix->args_arena->current_offset = 0;
    affix->ret_arena->current_offset = 0;

    // Inlined Arena Allocations
    // Manually "allocate" the single block for all marshalled C arguments.
    void * args_buffer = affix->args_arena->buffer;
    affix->args_arena->current_offset += affix->total_args_size;

    // Manually "allocate" the block for the C function's return value.
    void * ret_buffer = affix->ret_arena->buffer;
    affix->ret_arena->current_offset += affix->ret_type->size;


    // Main Execution Loop
    for (size_t i = 0; i < affix->plan_length; ++i)
        affix->plan[i].executor(aTHX_ affix, &affix->plan[i], perl_stack_frame, args_buffer, affix->c_args, ret_buffer);


    // Out-Parameter Write-Back
#if 0  // INFIX_OS_WINDOWS
    for (size_t i = 0; i < affix->num_out_params; ++i) {
        const OutParamInfo * info = &affix->out_param_info[i];
        SV * arg_sv = perl_stack_frame[info->perl_stack_index];
        if (SvROK(arg_sv) && !is_pin(aTHX_ arg_sv)) {
            SV * rsv = SvRV(arg_sv);
            if (SvTYPE(rsv) == SVt_PVAV)
                continue;
            info->writer(aTHX_ affix, info, rsv, affix->c_args[info->perl_stack_index]);
        }
    }
#else
    // if (affix->num_out_params > 0) {
    //  Phase 1: Filter
    //  Create a temporary, stack-allocated list of indices for valid write-back candidates.
    size_t valid_out_indices[affix->num_out_params];
    size_t num_valid_out_params = 0;

    // This is now the ONLY place in the hot path where we do expensive checks.
    for (size_t i = 0; i < affix->num_out_params; ++i) {
        const OutParamInfo * info = &affix->out_param_info[i];
        SV * arg_sv = perl_stack_frame[info->perl_stack_index];

        // Perform the expensive checks once and filter.
        if (SvROK(arg_sv) && !is_pin(aTHX_ arg_sv)) {
            // This is a valid candidate. Add its index to our list.
            valid_out_indices[num_valid_out_params++] = i;
        }
    }

    // Phase 2: Execute
    // This loop is now branch-free and highly predictable.
    for (size_t i = 0; i < num_valid_out_params; ++i) {
        size_t info_idx = valid_out_indices[i];
        const OutParamInfo * info = &affix->out_param_info[info_idx];

        // We already know it's a valid reference, so we can skip the checks.
        SV * rsv = SvRV(perl_stack_frame[info->perl_stack_index]);

        // Skip AVs which are handled differently (in-place modification during the call)
        if (SvTYPE(rsv) == SVt_PVAV)
            continue;

        // Execute the pre-resolved writer function directly.
        info->writer(aTHX_ affix, info, rsv, affix->c_args[info->perl_stack_index]);
    }
    //}
#endif
    //
    ST(0) = affix->return_sv;
    PL_stack_sp = PL_stack_base + ax;
    return;
}

static infix_library_t * _get_lib_from_registry(pTHX_ const char * path) {
    dMY_CXT;
    const char * lookup_path = (path == NULL) ? "" : path;
    SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
    if (entry_sv_ptr) {
        LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
        entry->ref_count++;
        return entry->lib;
    }
    infix_library_t * lib = infix_library_open(path);
    if (lib) {
        LibRegistryEntry * new_entry;
        Newxz(new_entry, 1, LibRegistryEntry);
        new_entry->lib = lib;
        new_entry->ref_count = 1;
        hv_store(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), newSViv(PTR2IV(new_entry)), 0);
        return lib;
    }
    return NULL;
}

XS_INTERNAL(Affix_affix) {
    dXSARGS;
    dXSI32;
    dMY_CXT;

    // ---------------------------------------------------------
    // 1. Argument Parsing and Symbol Resolution (Shared)
    // ---------------------------------------------------------
    if (ix == 2 || ix == 4) {
        if (items != 3)
            croak_xs_usage(cv, "Affix::affix_bundle($target, $name, $signature)");
    }
    else {
        if (items != 3 && items != 4)
            croak_xs_usage(cv, "Affix::affix($target, $name_spec, $signature, [$return])");
    }

    void * symbol = NULL;
    char * rename = NULL;
    infix_library_t * lib_handle_for_symbol = NULL;
    bool created_implicit_handle = false;
    SV * target_sv = ST(0);
    SV * name_sv = ST(1);
    const char * symbol_name_str = NULL;
    const char * rename_str = NULL;

    if (SvROK(name_sv) && SvTYPE(SvRV(name_sv)) == SVt_PVAV) {
        if (ix == 1 || ix == 3)
            croak("Cannot rename an anonymous Affix'd wrapper");
        AV * name_av = (AV *)SvRV(name_sv);
        if (av_count(name_av) != 2)
            croak("Name spec arrayref must contain exactly two elements: [symbol_name, new_sub_name]");
        symbol_name_str = SvPV_nolen(*av_fetch(name_av, 0, 0));
        rename_str = SvPV_nolen(*av_fetch(name_av, 1, 0));
    }
    else {
        symbol_name_str = rename_str = SvPV_nolen(name_sv);
    }
    rename = (char *)rename_str;

    if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
        IV tmp = SvIV((SV *)SvRV(target_sv));
        lib_handle_for_symbol = INT2PTR(infix_library_t *, tmp);
        created_implicit_handle = false;
    }
    else if (_get_pin_from_sv(aTHX_ target_sv)) {
        symbol = _get_pin_from_sv(aTHX_ target_sv)->pointer;
    }
    else {
        const char * path = SvOK(target_sv) ? SvPV_nolen(target_sv) : NULL;
        lib_handle_for_symbol = _get_lib_from_registry(aTHX_ path);
        if (lib_handle_for_symbol)
            created_implicit_handle = true;
    }

    if (lib_handle_for_symbol && !symbol)
        symbol = infix_library_get_symbol(lib_handle_for_symbol, symbol_name_str);

    if (symbol == NULL) {
        if (created_implicit_handle) {
            // Cleanup implicit handle if symbol not found
            const char * lookup_path = SvOK(target_sv) ? SvPV_nolen(target_sv) : "";
            SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
            if (entry_sv_ptr) {
                LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
                entry->ref_count--;
                if (entry->ref_count == 0) {
                    infix_library_close(entry->lib);
                    safefree(entry);
                    hv_delete_ent(MY_CXT.lib_registry, newSVpvn(lookup_path, strlen(lookup_path)), G_DISCARD, 0);
                }
            }
        }
        XSRETURN_UNDEF;
    }

    // ---------------------------------------------------------
    // 2. Path A: Affix Bundle (High-Performance JIT Backend)
    // ---------------------------------------------------------
    if (ix == 2) {
        Affix_Backend * backend;
        Newxz(backend, 1, Affix_Backend);

        infix_arena_t * parse_arena = NULL;
        infix_type * ret_type = NULL;
        infix_function_argument * args = NULL;
        size_t num_args = 0, num_fixed = 0;
        char * signature = SvPV_nolen(ST(2));

        infix_status status =
            infix_signature_parse(signature, &parse_arena, &ret_type, &args, &num_args, &num_fixed, MY_CXT.registry);

        if (status != INFIX_SUCCESS) {
            safefree(backend);
            croak("Failed to parse signature for affix_bundle: %s", infix_get_last_error().message);
        }

        infix_direct_arg_handler_t * handlers =
            (infix_direct_arg_handler_t *)safecalloc(num_args, sizeof(infix_direct_arg_handler_t));

        for (size_t i = 0; i < num_args; ++i)
            handlers[i] = get_direct_handler_for_type(args[i].type);

        status = infix_forward_create_direct(&backend->infix, signature, symbol, handlers, MY_CXT.registry);

        safefree(handlers);
        infix_arena_destroy(parse_arena);

        if (status != INFIX_SUCCESS) {
            safefree(backend);
            croak("Failed to create direct trampoline: %s", infix_get_last_error().message);
        }

        backend->cif = infix_forward_get_direct_code(backend->infix);
        backend->num_args = num_args;
        backend->ret_type = infix_forward_get_return_type(backend->infix);
        backend->pull_handler = get_pull_handler(backend->ret_type);

        if (!backend->pull_handler) {
            infix_forward_destroy(backend->infix);
            safefree(backend);
            croak("Unsupported return type for affix_bundle");
        }

        // Explicitly set lib_handle so we can clean it up later
        backend->lib_handle = created_implicit_handle ? lib_handle_for_symbol : NULL;

        char prototype_buf[256] = {0};
        for (size_t i = 0; i < backend->num_args; ++i)
            strcat(prototype_buf, "$");

        //    CV * cv_new = newXSproto_portable(ix == 0 ? rename : NULL, Affix_trigger, __FILE__, prototype_buf);
        CV * cv_new =
            newXSproto_portable((ix == 0 || ix == 2) ? rename : NULL, Affix_trigger_backend, __FILE__, prototype_buf);
        //~ CV * cv_new = newXSproto_portable(rename_str, Affix_trigger_backend, __FILE__, prototype_buf);

        CvXSUBANY(cv_new).any_ptr = (void *)backend;

        SV * obj = newRV_inc(MUTABLE_SV(cv_new));
        sv_bless(obj, gv_stashpv("Affix::Bundled", GV_ADD));
        ST(0) = sv_2mortal(obj);
        XSRETURN(1);
    }

    // ---------------------------------------------------------
    // 3. Path B: Standard Affix (Optimized Frontend)
    // ---------------------------------------------------------
    const char * signature = NULL;
    char signature_buf[1024] = {0};

    if (items == 4) {
        SV * args_sv = ST(2);
        SV * ret_sv = ST(3);

        if (!SvROK(args_sv) || SvTYPE(SvRV(args_sv)) != SVt_PVAV)
            croak("Usage: affix(..., \\@args, $ret_type) - 3rd argument must be an array reference of types");
        if (sv_isobject(ret_sv) && !sv_derived_from(ret_sv, "Affix::Type"))
            croak("Usage: affix(..., \\@args, $ret_type) - 4th argument must be an Affix::Type object");

        strcat(signature_buf, "(");
        AV * args_av = (AV *)SvRV(args_sv);
        SSize_t num_args = av_len(args_av) + 1;

        for (SSize_t i = 0; i < num_args; ++i) {
            SV ** type_sv_ptr = av_fetch(args_av, i, 0);
            if (!type_sv_ptr)
                continue;
            const char * arg_sig = _get_string_from_type_obj(aTHX_ * type_sv_ptr);
            if (!arg_sig)
                croak("Argument %d in signature array is not a valid Affix::Type object", (int)i + 1);
            strcat(signature_buf, arg_sig);
            if (i < num_args - 1)
                strcat(signature_buf, ",");
        }
        strcat(signature_buf, ") -> ");

        const char * ret_sig = _get_string_from_type_obj(aTHX_ ret_sv);
        if (!ret_sig)
            croak("Return type is not a valid Affix::Type object");
        strcat(signature_buf, ret_sig);
        signature = signature_buf;
    }
    else {
        SV * signature_sv = ST(2);
        signature = _get_string_from_type_obj(aTHX_ signature_sv);
        if (signature == NULL)
            signature = SvPV_nolen(signature_sv);
    }

    Affix * affix;
    Newxz(affix, 1, Affix);
    affix->return_sv = newSV(0);  // Kept for safety, though hot path uses TARG

    if (created_implicit_handle)
        affix->lib_handle = lib_handle_for_symbol;
    else
        affix->lib_handle = NULL;

    infix_status status = infix_forward_create(&affix->infix, signature, symbol, MY_CXT.registry);

    if (status != INFIX_SUCCESS) {
        SvREFCNT_dec(affix->return_sv);
        safefree(affix);
        croak("Failed to parse signature or create trampoline: %s", infix_get_last_error().message);
    }

    affix->cif = infix_forward_get_code(affix->infix);
    affix->num_args = infix_forward_get_num_args(affix->infix);
    affix->ret_type = infix_forward_get_return_type(affix->infix);

    // OPTIMIZATION: Pre-resolve the return handler here to avoid switch/lookup in hot path
    affix->ret_pull_handler = get_pull_handler(affix->ret_type);
    if (affix->ret_pull_handler == NULL) {
        infix_forward_destroy(affix->infix);
        SvREFCNT_dec(affix->return_sv);
        safefree(affix);
        croak("Unsupported return type in signature");
    }

    if (affix->num_args > 0)
        Newx(affix->c_args, affix->num_args, void *);
    else
        affix->c_args = NULL;

    affix->args_arena = infix_arena_create(4096);
    affix->ret_arena = infix_arena_create(1024);
    if (!affix->args_arena || !affix->ret_arena)
        croak("Failed to create memory arenas for FFI call");

    // OPTIMIZATION: Plan length is exactly num_args.
    // We do not create steps for "call" or "return".
    affix->plan_length = affix->num_args;
    if (affix->plan_length > 0)
        Newxz(affix->plan, affix->plan_length, Affix_Plan_Step);
    else
        affix->plan = NULL;

    size_t current_offset = 0;
    for (size_t i = 0; i < affix->num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        size_t alignment = (type->category == INFIX_TYPE_ARRAY) ? type->alignment : infix_type_get_alignment(type);
        if (alignment == 0)
            alignment = 1;

        current_offset = (current_offset + alignment - 1) & ~(alignment - 1);
        affix->plan[i].data.c_arg_offset = current_offset;

        size_t size = (type->category == INFIX_TYPE_ARRAY) ? type->size : infix_type_get_size(type);
        current_offset += size;
    }
    affix->total_args_size = current_offset;

    // Setup Execution Plan
    size_t out_param_count = 0;
    OutParamInfo * temp_out_info = safemalloc(sizeof(OutParamInfo) * (affix->num_args > 0 ? affix->num_args : 1));

    for (size_t i = 0; i < affix->num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        affix->plan[i].executor = get_plan_step_executor(type);
        affix->plan[i].opcode = get_opcode_for_type(type);

        if (affix->plan[i].executor == NULL) {
            safefree(temp_out_info);
            infix_forward_destroy(affix->infix);
            SvREFCNT_dec(affix->return_sv);
            infix_arena_destroy(affix->args_arena);
            infix_arena_destroy(affix->ret_arena);
            if (affix->plan)
                safefree(affix->plan);
            safefree(affix);
            croak("Unsupported argument type in signature at index %zu", i);
        }

        affix->plan[i].data.type = type;
        affix->plan[i].data.index = i;

        if (type->category == INFIX_TYPE_POINTER) {
            const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
            if (pointee_type->category != INFIX_TYPE_REVERSE_TRAMPOLINE && pointee_type->category != INFIX_TYPE_VOID) {
                temp_out_info[out_param_count].perl_stack_index = i;
                temp_out_info[out_param_count].pointee_type = pointee_type;
                temp_out_info[out_param_count].writer = get_out_param_writer(pointee_type);
                out_param_count++;
            }
        }
    }

    affix->num_out_params = out_param_count;
    if (out_param_count > 0) {
        affix->out_param_info = safemalloc(sizeof(OutParamInfo) * out_param_count);
        memcpy(affix->out_param_info, temp_out_info, sizeof(OutParamInfo) * out_param_count);
    }
    else {
        affix->out_param_info = NULL;
    }
    safefree(temp_out_info);

    // Create XSUB
    char prototype_buf[256] = {0};
    for (size_t i = 0; i < affix->num_args; ++i)
        strcat(prototype_buf, "$");

    CV * cv_new = newXSproto_portable(ix == 0 ? rename : NULL, Affix_trigger, __FILE__, prototype_buf);
    if (UNLIKELY(cv_new == NULL))
        croak("Failed to install new XSUB");

    CvXSUBANY(cv_new).any_ptr = (void *)affix;
    SV * obj = newRV_inc(MUTABLE_SV(cv_new));
    sv_bless(obj, gv_stashpv("Affix", GV_ADD));
    ST(0) = sv_2mortal(obj);
    XSRETURN(1);
}

XS_INTERNAL(ssssAffix_affix) {
    dXSARGS;
    dXSI32;
    dMY_CXT;


    // Multiplex to the new backend logic if `affix_bundle` was called.
    if (ix == 2 || ix == 3) {
        if (items != 3)
            croak_xs_usage(cv, "Affix::affix_bundle($target, $name, $signature)");


        void * symbol = NULL;
        char * rename = NULL;
        infix_library_t * lib_handle_for_symbol = NULL;
        bool created_implicit_handle = false;
        SV * target_sv = ST(0);
        SV * name_sv = ST(1);
        const char * symbol_name_str = NULL;
        const char * rename_str = NULL;
        if (SvROK(name_sv) && SvTYPE(SvRV(name_sv)) == SVt_PVAV) {
            if (ix)
                croak("Cannot rename an anonymous Affix'd wrapper");
            AV * name_av = (AV *)SvRV(name_sv);
            if (av_count(name_av) != 2)
                croak("Name spec arrayref must contain exactly two elements: [symbol_name, new_sub_name]");
            symbol_name_str = SvPV_nolen(*av_fetch(name_av, 0, 0));
            rename_str = SvPV_nolen(*av_fetch(name_av, 1, 0));
        }
        else
            symbol_name_str = rename_str = SvPV_nolen(name_sv);
        rename = (char *)rename_str;
        if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
            IV tmp = SvIV((SV *)SvRV(target_sv));
            lib_handle_for_symbol = INT2PTR(infix_library_t *, tmp);
            created_implicit_handle = false;
        }
        else if (_get_pin_from_sv(aTHX_ target_sv))
            symbol = _get_pin_from_sv(aTHX_ target_sv)->pointer;
        else {
            const char * path = SvOK(target_sv) ? SvPV_nolen(target_sv) : NULL;
            lib_handle_for_symbol = _get_lib_from_registry(aTHX_ path);
            if (lib_handle_for_symbol)
                created_implicit_handle = true;
        }
        if (lib_handle_for_symbol && !symbol)
            symbol = infix_library_get_symbol(lib_handle_for_symbol, symbol_name_str);
        if (symbol == NULL) {
            if (created_implicit_handle) {
                const char * lookup_path = SvOK(target_sv) ? SvPV_nolen(target_sv) : "";
                SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
                if (entry_sv_ptr) {
                    LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
                    entry->ref_count--;
                    if (entry->ref_count == 0) {
                        infix_library_close(entry->lib);
                        safefree(entry);
                        hv_delete_ent(MY_CXT.lib_registry, newSVpvn(lookup_path, strlen(lookup_path)), G_DISCARD, 0);
                    }
                }
            }
            XSRETURN_UNDEF;
        }


        // --- Backend Implementation ---
        Affix_Backend * backend;
        Newxz(backend, 1, Affix_Backend);

        // Parse signature to get arg types for handler resolution.
        infix_arena_t * parse_arena = NULL;
        infix_type * ret_type = NULL;
        infix_function_argument * args = NULL;
        size_t num_args = 0, num_fixed = 0;
        char * signature = SvPV_nolen(ST(2));

        infix_status status =
            infix_signature_parse(signature, &parse_arena, &ret_type, &args, &num_args, &num_fixed, MY_CXT.registry);
        if (status != INFIX_SUCCESS) {
            safefree(backend);
            croak("Failed to parse signature for affix_bundle: %s", infix_get_last_error().message);
        }

        // Build the array of handlers.
        infix_direct_arg_handler_t * handlers =
            (infix_direct_arg_handler_t *)safecalloc(num_args, sizeof(infix_direct_arg_handler_t));
        for (size_t i = 0; i < num_args; ++i)
            handlers[i] = get_direct_handler_for_type(args[i].type);

        // Create the direct marshalling trampoline.
        status = infix_forward_create_direct(&backend->infix, signature, symbol, handlers, MY_CXT.registry);

        safefree(handlers);
        infix_arena_destroy(parse_arena);

        if (status != INFIX_SUCCESS) {
            safefree(backend);
            croak("Failed to create direct trampoline: %s", infix_get_last_error().message);
        }

        backend->cif = infix_forward_get_direct_code(backend->infix);
        backend->num_args = num_args;
        backend->ret_type = infix_forward_get_return_type(backend->infix);
        backend->pull_handler = get_pull_handler(backend->ret_type);
        if (!backend->pull_handler) {
            infix_forward_destroy(backend->infix);
            safefree(backend);
            croak("Unsupported return type for affix_bundle");
        }

        char prototype_buf[256] = {0};
        for (size_t i = 0; i < backend->num_args; ++i)
            strcat(prototype_buf, "$");

        CV * cv_new = newXSproto_portable(ix == 2 ? rename_str : NULL, Affix_trigger_backend, __FILE__, prototype_buf);
        CvXSUBANY(cv_new).any_ptr = (void *)backend;

        SV * obj = newRV_inc(MUTABLE_SV(cv_new));
        sv_bless(obj, gv_stashpv("Affix::Bundled", GV_ADD));  // Bless into a new class
        ST(0) = sv_2mortal(obj);
        XSRETURN(1);
    }


    if (items != 3 && items != 4)
        croak_xs_usage(cv, "Affix::affix($target, $name_spec, $signature, [$return])");
    void * symbol = NULL;
    char * rename = NULL;
    infix_library_t * lib_handle_for_symbol = NULL;
    bool created_implicit_handle = false;
    SV * target_sv = ST(0);
    SV * name_sv = ST(1);
    const char * symbol_name_str = NULL;
    const char * rename_str = NULL;
    if (SvROK(name_sv) && SvTYPE(SvRV(name_sv)) == SVt_PVAV) {
        if (ix)
            croak("Cannot rename an anonymous Affix'd wrapper");
        AV * name_av = (AV *)SvRV(name_sv);
        if (av_count(name_av) != 2)
            croak("Name spec arrayref must contain exactly two elements: [symbol_name, new_sub_name]");
        symbol_name_str = SvPV_nolen(*av_fetch(name_av, 0, 0));
        rename_str = SvPV_nolen(*av_fetch(name_av, 1, 0));
    }
    else
        symbol_name_str = rename_str = SvPV_nolen(name_sv);
    rename = (char *)rename_str;
    if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
        IV tmp = SvIV((SV *)SvRV(target_sv));
        lib_handle_for_symbol = INT2PTR(infix_library_t *, tmp);
        created_implicit_handle = false;
    }
    else if (_get_pin_from_sv(aTHX_ target_sv))
        symbol = _get_pin_from_sv(aTHX_ target_sv)->pointer;
    else {
        const char * path = SvOK(target_sv) ? SvPV_nolen(target_sv) : NULL;
        lib_handle_for_symbol = _get_lib_from_registry(aTHX_ path);
        if (lib_handle_for_symbol)
            created_implicit_handle = true;
    }
    if (lib_handle_for_symbol && !symbol)
        symbol = infix_library_get_symbol(lib_handle_for_symbol, symbol_name_str);
    if (symbol == NULL) {
        if (created_implicit_handle) {
            const char * lookup_path = SvOK(target_sv) ? SvPV_nolen(target_sv) : "";
            SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
            if (entry_sv_ptr) {
                LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
                entry->ref_count--;
                if (entry->ref_count == 0) {
                    infix_library_close(entry->lib);
                    safefree(entry);
                    hv_delete_ent(MY_CXT.lib_registry, newSVpvn(lookup_path, strlen(lookup_path)), G_DISCARD, 0);
                }
            }
        }
        XSRETURN_UNDEF;
    }


    const char * signature = NULL;
    char signature_buf[1024] = {0};

    if (items == 4) {
        // 4-argument form: affix($lib, $name, \@args, $ret)
        SV * args_sv = ST(2);
        SV * ret_sv = ST(3);

        if (!SvROK(args_sv) || SvTYPE(SvRV(args_sv)) != SVt_PVAV)
            croak("Usage: affix(..., \\@args, $ret_type) - 3rd argument must be an array reference of types");
        if (sv_isobject(ret_sv) && !sv_derived_from(ret_sv, "Affix::Type"))
            croak("Usage: affix(..., \\@args, $ret_type) - 4th argument must be an Affix::Type object");

        // Build the signature string: "(arg1,arg2,...) -> ret"
        strcat(signature_buf, "(");
        AV * args_av = (AV *)SvRV(args_sv);
        SSize_t num_args = av_len(args_av) + 1;

        for (SSize_t i = 0; i < num_args; ++i) {
            SV ** type_sv_ptr = av_fetch(args_av, i, 0);
            if (!type_sv_ptr)
                continue;

            const char * arg_sig = _get_string_from_type_obj(aTHX_ * type_sv_ptr);
            if (!arg_sig)
                croak("Argument %d in signature array is not a valid Affix::Type object", (int)i + 1);
            strcat(signature_buf, arg_sig);
            if (i < num_args - 1)
                strcat(signature_buf, ",");
        }
        strcat(signature_buf, ") -> ");

        const char * ret_sig = _get_string_from_type_obj(aTHX_ ret_sv);
        if (!ret_sig)
            croak("Return type is not a valid Affix::Type object");
        strcat(signature_buf, ret_sig);

        signature = signature_buf;
    }
    else {  // items == 3
        // 3-argument form: affix($lib, $name, $signature)
        SV * signature_sv = ST(2);
        signature = _get_string_from_type_obj(aTHX_ signature_sv);
        if (signature == NULL) {
            // Not a valid object, so assume it's a plain string
            signature = SvPV_nolen(signature_sv);
        }
    }


    Affix * affix;
    Newxz(affix, 1, Affix);
    affix->return_sv = newSV(0);
    if (created_implicit_handle)
        affix->lib_handle = lib_handle_for_symbol;
    else
        affix->lib_handle = NULL;

    infix_status status = infix_forward_create(&affix->infix, signature, symbol, MY_CXT.registry);
    if (status != INFIX_SUCCESS) {
        SvREFCNT_dec(affix->return_sv);
        safefree(affix);
        croak("Failed to parse signature or create trampoline: %s", infix_get_last_error().message);
    }
    affix->cif = infix_forward_get_code(affix->infix);
    affix->num_args = infix_forward_get_num_args(affix->infix);
    affix->ret_type = infix_forward_get_return_type(affix->infix);

    if (affix->num_args > 0)
        Newx(affix->c_args, affix->num_args, void *);
    else
        affix->c_args = NULL;

    affix->args_arena = infix_arena_create(4096);
    affix->ret_arena = infix_arena_create(1024);
    if (!affix->args_arena || !affix->ret_arena)
        croak("Failed to create memory arenas for FFI call");

    affix->plan_length = affix->num_args + 2;
    Newxz(affix->plan, affix->plan_length, Affix_Plan_Step);

    size_t current_offset = 0;
    for (size_t i = 0; i < affix->num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        size_t alignment = (type->category == INFIX_TYPE_ARRAY) ? type->alignment : infix_type_get_alignment(type);
        if (alignment == 0)
            alignment = 1;
        current_offset = (current_offset + alignment - 1) & ~(alignment - 1);
        affix->plan[i].data.c_arg_offset = current_offset;
        size_t size = (type->category == INFIX_TYPE_ARRAY) ? type->size : infix_type_get_size(type);
        current_offset += size;
    }
    affix->total_args_size = current_offset;

    size_t out_param_count = 0;
    OutParamInfo * temp_out_info = safemalloc(sizeof(OutParamInfo) * affix->num_args);
    for (size_t i = 0; i < affix->num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        affix->plan[i].executor = get_plan_step_executor(type);
        affix->plan[i].opcode = get_opcode_for_type(type);
        if (affix->plan[i].executor == NULL) {
            safefree(temp_out_info);
            infix_forward_destroy(affix->infix);
            SvREFCNT_dec(affix->return_sv);
            infix_arena_destroy(affix->args_arena);
            infix_arena_destroy(affix->ret_arena);
            safefree(affix->plan);
            safefree(affix);
            croak("Unsupported argument type in signature at index %zu", i);
        }
        affix->plan[i].data.type = type;
        affix->plan[i].data.index = i;
        if (type->category == INFIX_TYPE_POINTER) {
            const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
            if (pointee_type->category != INFIX_TYPE_REVERSE_TRAMPOLINE && pointee_type->category != INFIX_TYPE_VOID) {
                temp_out_info[out_param_count].perl_stack_index = i;
                temp_out_info[out_param_count].pointee_type = pointee_type;
                temp_out_info[out_param_count].writer = get_out_param_writer(pointee_type);
                out_param_count++;
            }
        }
    }
    affix->num_out_params = out_param_count;
    if (out_param_count > 0) {
        affix->out_param_info = safemalloc(sizeof(OutParamInfo) * out_param_count);
        memcpy(affix->out_param_info, temp_out_info, sizeof(OutParamInfo) * out_param_count);
    }
    else {
        affix->out_param_info = NULL;
    }
    safefree(temp_out_info);
    affix->plan[affix->num_args].executor = plan_step_call_c_function;
    affix->plan[affix->num_args].data.type = NULL;
    affix->plan[affix->num_args].data.index = 0;

    affix->plan[affix->num_args + 1].executor = plan_step_pull_return_value;
    affix->plan[affix->num_args + 1].data.type = affix->ret_type;
    affix->plan[affix->num_args + 1].data.pull_handler = get_pull_handler(affix->ret_type);
    if (affix->plan[affix->num_args + 1].data.pull_handler == NULL) {
        infix_forward_destroy(affix->infix);
        infix_arena_destroy(affix->args_arena);
        infix_arena_destroy(affix->ret_arena);
        safefree(affix->plan);
        SvREFCNT_dec(affix->return_sv);
        if (affix->out_param_info)
            safefree(affix->out_param_info);
        safefree(affix);
        croak("Unsupported return type in signature");
    }
    char prototype_buf[256] = {0};
    for (size_t i = 0; i < affix->num_args; ++i)
        strcat(prototype_buf, "$");
    CV * cv_new = newXSproto_portable(ix == 0 ? rename : NULL, Affix_trigger, __FILE__, prototype_buf);
    if (UNLIKELY(cv_new == NULL))
        croak("Failed to install new XSUB");
    CvXSUBANY(cv_new).any_ptr = (void *)affix;
    SV * obj = newRV_inc(MUTABLE_SV(cv_new));
    sv_bless(obj, gv_stashpv("Affix", GV_ADD));
    ST(0) = sv_2mortal(obj);
    XSRETURN(1);
}
XS_INTERNAL(Affix_Bundled_DESTROY) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    Affix_Backend * backend;
    STMT_START {
        HV * st;
        GV * gvp;
        SV * const xsub_tmp_sv = ST(0);
        SvGETMAGIC(xsub_tmp_sv);
        CV * cv_ptr = sv_2cv(xsub_tmp_sv, &st, &gvp, 0);
        backend = (Affix_Backend *)CvXSUBANY(cv_ptr).any_ptr;
    }
    STMT_END;

    if (backend) {
        if (backend->infix)
            infix_forward_destroy(backend->infix);
        // lib_handle cleanup would also go here.
        safefree(backend);
    }
    XSRETURN_EMPTY;
}
XS_INTERNAL(Affix_DESTROY) {
    dXSARGS;
    dMY_CXT;
    PERL_UNUSED_VAR(items);
    Affix * affix;
    STMT_START {
        HV * st;
        GV * gvp;
        SV * const xsub_tmp_sv = ST(0);
        SvGETMAGIC(xsub_tmp_sv);
        CV * cv_ptr = sv_2cv(xsub_tmp_sv, &st, &gvp, 0);
        affix = (Affix *)CvXSUBANY(cv_ptr).any_ptr;
    }
    STMT_END;
    if (affix != NULL) {
        if (affix->lib_handle != NULL && MY_CXT.lib_registry != NULL) {
            hv_iterinit(MY_CXT.lib_registry);
            HE * he;
            while ((he = hv_iternext(MY_CXT.lib_registry))) {
                SV * entry_sv = HeVAL(he);
                LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
                if (entry->lib == affix->lib_handle) {
                    entry->ref_count--;
                    if (entry->ref_count == 0) {
                        infix_library_close(entry->lib);
                        safefree(entry);
                        hv_delete_ent(MY_CXT.lib_registry, HeKEY_sv(he), G_DISCARD, 0);
                    }
                    break;
                }
            }
        }
        if (affix->return_sv)
            SvREFCNT_dec(affix->return_sv);
        if (affix->args_arena != NULL)
            infix_arena_destroy(affix->args_arena);
        if (affix->ret_arena != NULL)
            infix_arena_destroy(affix->ret_arena);
        if (affix->infix != NULL)
            infix_forward_destroy(affix->infix);
        if (affix->plan != NULL)
            safefree(affix->plan);
        if (affix->out_param_info != NULL)
            safefree(affix->out_param_info);
        if (affix->c_args != NULL)
            safefree(affix->c_args);
        safefree(affix);
    }
    XSRETURN_EMPTY;
}

static void pull_sint8(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setiv(sv, *(int8_t *)p);
}
static void pull_uint8(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setuv(sv, *(uint8_t *)p);
}
static void pull_sint16(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setiv(sv, *(int16_t *)p);
}
static void pull_uint16(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setuv(sv, *(uint16_t *)p);
}
static void pull_sint32(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setiv(sv, *(int32_t *)p);
}
static void pull_uint32(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setuv(sv, *(uint32_t *)p);
}
static void pull_sint64(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setiv(sv, *(int64_t *)p);
}
static void pull_uint64(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setuv(sv, *(uint64_t *)p);
}
static void pull_float(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setnv(sv, *(float *)p);
}
static void pull_double(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setnv(sv, *(double *)p);
}
static void pull_long_double(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setnv(sv, *(long double *)p);
}
static void pull_bool(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    sv_setbool(sv, *(bool *)p);
}
static void pull_void(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(t);
    PERL_UNUSED_VAR(p);
    sv_setsv(sv, &PL_sv_undef);
}
#if !defined(INFIX_COMPILER_MSVC)
static void pull_sint128(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
static void pull_uint128(pTHX_ Affix * affix, SV * sv, const infix_type * t, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
#endif

static void pull_struct(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    HV * hv;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVHV)
        hv = (HV *)SvRV(sv);
    else {
        hv = newHV();
        sv_setsv(sv, sv_2mortal(newRV_noinc(MUTABLE_SV(hv))));
    }
    _populate_hv_from_c_struct(aTHX_ affix, hv, type, p);
}
static void pull_union(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    croak("Cannot pull a C union directly; the active member is unknown.");
}
static void pull_array(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    const infix_type * element_type = type->meta.array_info.element_type;
    if (element_type->category == INFIX_TYPE_PRIMITIVE &&
        (element_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         element_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
        sv_setpv(sv, (const char *)p);
        return;
    }
    AV * av;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVAV) {
        av = (AV *)SvRV(sv);
        av_clear(av);
    }
    else {
        av = newAV();
        sv_setsv(sv, sv_2mortal(newRV_noinc(MUTABLE_SV(av))));
    }
    size_t num_elements = type->meta.array_info.num_elements;
    size_t element_size = infix_type_get_size(element_type);
    av_extend(av, num_elements);
    for (size_t i = 0; i < num_elements; ++i) {
        void * element_ptr = (char *)p + (i * element_size);
        SV * element_sv = newSV(0);
        ptr2sv(aTHX_ affix, element_ptr, element_sv, element_type);
        av_push(av, element_sv);
    }
}
static void pull_reverse_trampoline(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(type);
    sv_setiv(sv, PTR2IV(*(void **)p));
}
static void pull_enum(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    ptr2sv(aTHX_ affix, p, sv, type->meta.enum_info.underlying_type);
}
static void pull_complex(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    AV * av;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVAV) {
        av = (AV *)SvRV(sv);
        av_clear(av);
    }
    else {
        av = newAV();
        sv_setsv(sv, sv_2mortal(newRV_noinc(MUTABLE_SV(av))));
    }
    const infix_type * base_type = type->meta.complex_info.base_type;
    size_t base_size = infix_type_get_size(base_type);
    SV * real_sv = newSV(0);
    ptr2sv(aTHX_ affix, p, real_sv, base_type);
    av_push(av, real_sv);
    SV * imag_sv = newSV(0);
    ptr2sv(aTHX_ affix, (char *)p + base_size, imag_sv, base_type);
    av_push(av, imag_sv);
}
static void pull_vector(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * p) {
    AV * av;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVAV) {
        av = (AV *)SvRV(sv);
        av_clear(av);
    }
    else {
        av = newAV();
        sv_setsv(sv, sv_2mortal(newRV_noinc(MUTABLE_SV(av))));
    }
    const infix_type * element_type = type->meta.vector_info.element_type;
    size_t num_elements = type->meta.vector_info.num_elements;
    size_t element_size = infix_type_get_size(element_type);
    av_extend(av, num_elements);
    for (size_t i = 0; i < num_elements; ++i) {
        void * element_ptr = (char *)p + (i * element_size);
        SV * element_sv = newSV(0);
        ptr2sv(aTHX_ affix, element_ptr, element_sv, element_type);
        av_push(av, element_sv);
    }
}

static void pull_pointer_as_string(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(type);
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL)
        sv_setsv(sv, &PL_sv_undef);
    else
        sv_setpv(sv, (const char *)c_ptr);
}

static void pull_pointer_as_struct(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL)
        sv_setsv(sv, &PL_sv_undef);
    else {
        const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
        pull_struct(aTHX_ affix, sv, pointee_type, c_ptr);
    }
}

static void pull_pointer_as_array(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL) {
        sv_setsv(sv, &PL_sv_undef);
    }
    else {
        const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
        pull_array(aTHX_ affix, sv, pointee_type, c_ptr);
    }
}

static void pull_pointer_as_pin(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    PERL_UNUSED_VAR(affix);
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL) {
        sv_setsv(sv, &PL_sv_undef);
        return;
    }

    // This is the default case for all other pointers (*int, **char, *void, etc.)
    // It unconditionally creates a pin object.
    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);

    SV * obj_data = newSV(0);
    sv_setiv(obj_data, PTR2IV(pin));


    SV * rv = sv_2mortal(newRV_noinc(obj_data));
    sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);

    sv_setsv(sv, rv);
    sv_bless(sv, gv_stashpv("Affix::Pin", GV_ADD));

    pin->pointer = c_ptr;
    pin->type = type;
    pin->managed = false;
}


static void pull_sv(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    PERL_UNUSED_VAR(affix);
    PERL_UNUSED_VAR(type);
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL)
        sv_setsv(sv, &PL_sv_undef);
    else
        sv_setsv(sv, (SV *)c_ptr);
}

static const Affix_Pull pull_handlers[] = {[INFIX_PRIMITIVE_BOOL] = pull_bool,
                                           [INFIX_PRIMITIVE_SINT8] = pull_sint8,
                                           [INFIX_PRIMITIVE_UINT8] = pull_uint8,
                                           [INFIX_PRIMITIVE_SINT16] = pull_sint16,
                                           [INFIX_PRIMITIVE_UINT16] = pull_uint16,
                                           [INFIX_PRIMITIVE_SINT32] = pull_sint32,
                                           [INFIX_PRIMITIVE_UINT32] = pull_uint32,
                                           [INFIX_PRIMITIVE_SINT64] = pull_sint64,
                                           [INFIX_PRIMITIVE_UINT64] = pull_uint64,
                                           [INFIX_PRIMITIVE_FLOAT] = pull_float,
                                           [INFIX_PRIMITIVE_DOUBLE] = pull_double,
                                           [INFIX_PRIMITIVE_LONG_DOUBLE] = pull_long_double,
#if !defined(INFIX_COMPILER_MSVC)
                                           [INFIX_PRIMITIVE_SINT128] = pull_sint128,
                                           [INFIX_PRIMITIVE_UINT128] = pull_uint128
#endif
};
Affix_Pull get_pull_handler(const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        return pull_handlers[type->meta.primitive_id];
    case INFIX_TYPE_POINTER:
        {
            const char * name = infix_type_get_name(type);

            // Special Case: "SV" -> pull_sv
            if (name && strnEQ(name, "SV", 2))
                return pull_sv;

            // CRITICAL FIX: If a pointer type has a semantic name (e.g., "HeapString"),
            // we force it to return a Pin object. This allows the user to keep the
            // pointer alive for functions like uiFreeText(), while still accessing
            // the data via magic dereferencing.
            if (name != NULL)
                return pull_pointer_as_pin;

            const infix_type * pointee_type = type->meta.pointer_info.pointee_type;

            if (pointee_type->category == INFIX_TYPE_PRIMITIVE &&
                (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
                 pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
                return pull_pointer_as_string;
            }
            if (pointee_type->category == INFIX_TYPE_STRUCT)
                return pull_pointer_as_struct;
            if (pointee_type->category == INFIX_TYPE_ARRAY)
                return pull_pointer_as_array;

            // Default for all other pointers is to return a pin object.
            return pull_pointer_as_pin;
        }
    case INFIX_TYPE_STRUCT:
        return pull_struct;
    case INFIX_TYPE_UNION:
        return pull_union;
    case INFIX_TYPE_ARRAY:
        return pull_array;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        return pull_reverse_trampoline;
    case INFIX_TYPE_ENUM:
        return pull_enum;
    case INFIX_TYPE_COMPLEX:
        return pull_complex;
    case INFIX_TYPE_VECTOR:
        return pull_vector;
    case INFIX_TYPE_VOID:
        return pull_void;
    default:
        return NULL;
    }
}
void ptr2sv(pTHX_ Affix * affix, void * c_ptr, SV * perl_sv, const infix_type * type) {
    Affix_Pull h = get_pull_handler(type);
    if (!h) {
        char buffer[128];
        if (infix_type_print(buffer, sizeof(buffer), type, INFIX_DIALECT_SIGNATURE) == INFIX_SUCCESS)
            croak("Cannot convert C type to Perl SV. Unsupported type: %s", buffer);
        croak("Cannot convert C type to Perl SV. Unsupported type.");
    }
    h(aTHX_ affix, perl_sv, type, c_ptr);
}

void sv2ptr(pTHX_ Affix * affix, SV * perl_sv, void * c_ptr, const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        primitive_push_handlers[type->meta.primitive_id](aTHX_ affix, perl_sv, c_ptr);
        break;
    case INFIX_TYPE_POINTER:
        {
            const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
            if (pointee_type->category == INFIX_TYPE_REVERSE_TRAMPOLINE) {
                push_reverse_trampoline(aTHX_ affix, pointee_type, perl_sv, c_ptr);
                return;
            }
            if (is_pin(aTHX_ perl_sv))
                *(void **)c_ptr = _get_pin_from_sv(aTHX_ perl_sv)->pointer;
            else if (!SvOK(perl_sv))
                *(void **)c_ptr = NULL;
            else if (SvPOK(perl_sv))
                *(const char **)c_ptr = SvPV_nolen(perl_sv);
            else if (SvROK(perl_sv) && SvTYPE(SvRV(perl_sv)) == SVt_PVAV) {
                AV * av = (AV *)SvRV(perl_sv);
                size_t len = av_len(av) + 1;
                size_t element_size = infix_type_get_size(pointee_type);
                size_t total_size = len * element_size;
                char * c_array;
                Newx(c_array, total_size, char);
                for (size_t i = 0; i < len; ++i) {
                    SV ** elem_sv_ptr = av_fetch(av, i, 0);
                    if (elem_sv_ptr)
                        sv2ptr(aTHX_ affix, *elem_sv_ptr, c_array + (i * element_size), pointee_type);
                }
                *(void **)c_ptr = c_array;
            }
            else {
                char signature_buf[256];
                (void)infix_type_print(
                    signature_buf, sizeof(signature_buf), (infix_type *)type, INFIX_DIALECT_SIGNATURE);
                croak("sv2ptr cannot handle this kind of pointer conversion yet: %s", signature_buf);
            }
        }
        break;
    case INFIX_TYPE_STRUCT:
        push_struct(aTHX_ affix, type, perl_sv, c_ptr);
        break;
    case INFIX_TYPE_ARRAY:
        push_array(aTHX_ affix, type, perl_sv, c_ptr);
        break;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        push_reverse_trampoline(aTHX_ affix, type, perl_sv, c_ptr);
        break;
    case INFIX_TYPE_ENUM:
        sv2ptr(aTHX_ affix, perl_sv, c_ptr, type->meta.enum_info.underlying_type);
        break;
    default:
        croak("sv2ptr cannot convert this complex type");
        break;
    }
}
void push_struct(pTHX_ Affix * affix, const infix_type * type, SV * sv, void * p) {
    HV * hv;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVHV)
        hv = (HV *)SvRV(sv);
    else if (SvTYPE(sv) == SVt_PVHV)
        hv = (HV *)sv;
    else
        croak("Expected a HASH or HASH reference for struct marshalling");
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (!member->name)
            continue;
        void * member_ptr = (char *)p + member->offset;
        SV ** member_sv_ptr = hv_fetch(hv, member->name, strlen(member->name), 0);
        if (member_sv_ptr)
            sv2ptr(aTHX_ affix, *member_sv_ptr, member_ptr, member->type);
    }
}
void push_array(pTHX_ Affix * affix, const infix_type * type, SV * sv, void * p) {
    const infix_type * element_type = type->meta.array_info.element_type;
    size_t c_array_len = type->meta.array_info.num_elements;
    if (element_type->category == INFIX_TYPE_PRIMITIVE &&
        (element_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         element_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8) &&
        SvPOK(sv)) {
        STRLEN perl_len;
        const char * perl_str = SvPV(sv, perl_len);
        if (perl_len >= c_array_len) {
            memcpy(p, perl_str, c_array_len - 1);
            ((char *)p)[c_array_len - 1] = '\0';
        }
        else
            memcpy(p, perl_str, perl_len + 1);
        return;
    }
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference for array marshalling");
    AV * av = (AV *)SvRV(sv);
    size_t perl_array_len = av_len(av) + 1;
    size_t num_to_copy = perl_array_len < c_array_len ? perl_array_len : c_array_len;
    if (perl_array_len > c_array_len)
        warn("Perl array has more elements (%lu) than C array capacity (%lu). Truncating.",
             (unsigned long)perl_array_len,
             (unsigned long)c_array_len);
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_to_copy; ++i) {
        SV ** element_sv_ptr = av_fetch(av, i, 0);
        if (element_sv_ptr) {
            void * element_ptr = (char *)p + (i * element_size);
            sv2ptr(aTHX_ affix, *element_sv_ptr, element_ptr, element_type);
        }
    }
}
void push_reverse_trampoline(pTHX_ Affix * affix, const infix_type * type, SV * sv, void * p) {
    PERL_UNUSED_VAR(affix);
    dMY_CXT;
    SV * coderef_cv = NULL;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVCV)
        coderef_cv = SvRV(sv);
    else if (SvTYPE(sv) == SVt_PVCV)
        coderef_cv = sv;
    if (coderef_cv) {
        char key[32];
        snprintf(key, sizeof(key), "%p", (void *)coderef_cv);
        SV ** entry_sv_ptr = hv_fetch(MY_CXT.callback_registry, key, strlen(key), 0);
        if (entry_sv_ptr) {
            Implicit_Callback_Magic * magic_data = INT2PTR(Implicit_Callback_Magic *, SvIV(*entry_sv_ptr));
            *(void **)p = infix_reverse_get_code(magic_data->reverse_ctx);
        }
        else {
            Affix_Callback_Data * cb_data;
            Newxz(cb_data, 1, Affix_Callback_Data);
            cb_data->coderef_rv = newRV_inc(coderef_cv);
            storeTHX(cb_data->perl);
            infix_type * ret_type = type->meta.func_ptr_info.return_type;
            size_t num_args = type->meta.func_ptr_info.num_args;
            size_t num_fixed_args = type->meta.func_ptr_info.num_fixed_args;
            infix_type ** arg_types = NULL;
            if (num_args > 0) {
                Newx(arg_types, num_args, infix_type *);
                for (size_t i = 0; i < num_args; ++i)
                    arg_types[i] = type->meta.func_ptr_info.args[i].type;
            }
            infix_reverse_t * reverse_ctx = NULL;

            infix_status status = infix_reverse_create_closure_manual(&reverse_ctx,
                                                                      ret_type,
                                                                      arg_types,
                                                                      num_args,
                                                                      num_fixed_args,
                                                                      (void *)_affix_callback_handler_entry,
                                                                      (void *)cb_data);
            if (arg_types)
                Safefree(arg_types);
            if (status != INFIX_SUCCESS) {
                SvREFCNT_dec(cb_data->coderef_rv);
                safefree(cb_data);
                char signature_buf[256];
                (void)infix_type_print(
                    signature_buf, sizeof(signature_buf), (infix_type *)type, INFIX_DIALECT_SIGNATURE);
                croak("Failed to create callback: %s", infix_get_last_error().message);
            }
            Implicit_Callback_Magic * magic_data;
            Newxz(magic_data, 1, Implicit_Callback_Magic);
            magic_data->reverse_ctx = reverse_ctx;
            hv_store(MY_CXT.callback_registry, key, strlen(key), newSViv(PTR2IV(magic_data)), 0);
            *(void **)p = infix_reverse_get_code(reverse_ctx);
        }
    }
    else if (!SvOK(sv))
        *(void **)p = NULL;
    else
        croak("Argument for a callback must be a code reference or undef.");
}
static SV * _format_parse_error(pTHX_ const char * context_msg, const char * signature, infix_error_details_t err) {
    STRLEN sig_len = strlen(signature);
    int radius = 20;
    size_t start = (err.position > radius) ? (err.position - radius) : 0;
    size_t end = (err.position + radius < sig_len) ? (err.position + radius) : sig_len;
    const char * start_indicator = (start > 0) ? "... " : "";
    const char * end_indicator = (end < sig_len) ? " ..." : "";
    int start_indicator_len = (start > 0) ? 4 : 0;
    char snippet[128];
    snprintf(
        snippet, sizeof(snippet), "%s%.*s%s", start_indicator, (int)(end - start), signature + start, end_indicator);
    char pointer[128];
    int caret_pos = err.position - start + start_indicator_len;
    snprintf(pointer, sizeof(pointer), "%*s^", caret_pos, "");
    return sv_2mortal(newSVpvf("Failed to parse signature %s:\n\n  %s\n  %s\n\nError: %s (at position %zu)",
                               context_msg,
                               snippet,
                               pointer,
                               err.message,
                               err.position));
}
XS_INTERNAL(Affix_Lib_as_string) {
    dVAR;
    dXSARGS;
    if (items < 1)
        croak_xs_usage(cv, "$lib");
    IV RETVAL;
    {
        infix_library_t * lib;
        IV tmp = SvIV((SV *)SvRV(ST(0)));
        lib = INT2PTR(infix_library_t *, tmp);
        RETVAL = PTR2IV(lib->handle);
    }
    XSRETURN_IV(RETVAL);
};
XS_INTERNAL(Affix_Lib_DESTROY) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "$lib");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    infix_library_t * lib = INT2PTR(infix_library_t *, tmp);
    if (MY_CXT.lib_registry) {
        hv_iterinit(MY_CXT.lib_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.lib_registry))) {
            SV * entry_sv = HeVAL(he);
            LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
            if (entry->lib == lib) {
                entry->ref_count--;
                if (entry->ref_count == 0) {
                    infix_library_close(entry->lib);
                    safefree(entry);
                    hv_delete_ent(MY_CXT.lib_registry, HeKEY_sv(he), G_DISCARD, 0);
                }
                break;
            }
        }
    }
    XSRETURN_EMPTY;
}
XS_INTERNAL(Affix_load_library) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "library_path");
    const char * path = SvPV_nolen(ST(0));
    SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, path, strlen(path), 0);
    if (entry_sv_ptr) {
        LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
        entry->ref_count++;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(entry->lib));
        ST(0) = sv_2mortal(sv_bless(newRV_inc(obj_data), gv_stashpv("Affix::Lib", GV_ADD)));
        XSRETURN(1);
    }
    infix_library_t * lib = infix_library_open(path);
    if (lib) {
        LibRegistryEntry * new_entry;
        Newxz(new_entry, 1, LibRegistryEntry);
        new_entry->lib = lib;
        new_entry->ref_count = 1;
        hv_store(MY_CXT.lib_registry, path, strlen(path), newSViv(PTR2IV(new_entry)), 0);
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(lib));
        ST(0) = sv_2mortal(sv_bless(newRV_inc(obj_data), gv_stashpv("Affix::Lib", GV_ADD)));
        XSRETURN(1);
    }
    XSRETURN_UNDEF;
}
XS_INTERNAL(Affix_get_last_error_message) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    infix_error_details_t err = infix_get_last_error();
    if (err.message[0] != '\0')
        ST(0) = sv_2mortal(newSVpv(err.message, 0));
#if defined(INFIX_OS_WINDOWS)
    else if (err.system_error_code != 0) {
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       err.system_error_code,
                       0,
                       buf,
                       sizeof(buf),
                       NULL);
        ST(0) = sv_2mortal(newSVpvf("System error: %s (code %ld)", buf, err.system_error_code));
    }
#endif
    else
        ST(0) = sv_2mortal(newSVpvf("Infix error code %d at position %zu", (int)err.code, err.position));
    XSRETURN(1);
}
Affix_Pin * _get_pin_from_sv(pTHX_ SV * sv) {
    if (!sv || !SvOK(sv) || !SvROK(sv) || !SvMAGICAL(SvRV(sv)))
        return NULL;
    MAGIC * mg = mg_findext(SvRV(sv), PERL_MAGIC_ext, &Affix_pin_vtbl);
    if (mg)
        return (Affix_Pin *)mg->mg_ptr;
    return NULL;
}
static int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type)
        return 0;
    const infix_type * type_to_marshal;
    if (pin->type->category == INFIX_TYPE_POINTER) {
        type_to_marshal = pin->type->meta.pointer_info.pointee_type;
        if (type_to_marshal->category == INFIX_TYPE_VOID) {
            if (pin->size > 0) {
                STRLEN perl_len;
                const char * perl_str = SvPV(sv, perl_len);
                size_t bytes_to_copy = (perl_len < pin->size) ? perl_len : pin->size;
                memcpy(pin->pointer, perl_str, bytes_to_copy);
                if (bytes_to_copy < pin->size)
                    memset((char *)pin->pointer + bytes_to_copy, 0, pin->size - bytes_to_copy);
                return 0;
            }
            else
                croak("Cannot assign a value to a dereferenced void pointer (opaque handle)");
        }
    }
    else
        type_to_marshal = pin->type;
    sv2ptr(aTHX_ NULL, sv, pin->pointer, type_to_marshal);
    return 0;
}
static U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type) {
        if (SvTYPE(sv) == SVt_PVAV)
            return av_len(MUTABLE_AV(sv));
        return sv_len(sv);
    }
    return pin->type->size;
}
static int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (pin == NULL)
        return 0;
    if (pin->managed && pin->pointer)
        safefree(pin->pointer);
    if (pin->type_arena != NULL)
        infix_arena_destroy(pin->type_arena);
    safefree(pin);
    mg->mg_ptr = NULL;
    return 0;
}
static int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer) {
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }

    // We don't strictly need to check pin->type here if we trust the creator,
    // but it's safer.
    if (pin->type && pin->type->category == INFIX_TYPE_POINTER) {
        const infix_type * pointee = pin->type->meta.pointer_info.pointee_type;

        // If it's a char* (like HeapString), copy the C string into the SV's PV slot.
        // This allows `print $$pin` to work.
        if (pointee->category == INFIX_TYPE_PRIMITIVE &&
            (pointee->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
             pointee->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {

            const char * str = (const char *)pin->pointer;
            if (str)
                sv_setpv(sv, str);
            else
                sv_setsv(sv, &PL_sv_undef);

            return 0;
        }
    }

    // Default fallback for other types (int*, etc)
    if (pin->type) {
        if (pin->type->category == INFIX_TYPE_POINTER &&
            pin->type->meta.pointer_info.pointee_type->category == INFIX_TYPE_VOID) {
            // Void pointer: return address as integer
            sv_setuv(sv, PTR2UV(pin->pointer));
        }
        else {
            // Typed pointer: marshal the value it points to
            ptr2sv(aTHX_ NULL, pin->pointer, sv, pin->type);
        }
    }
    return 0;
}
bool is_pin(pTHX_ SV * sv) {
    if (!sv || !SvOK(sv) || !SvROK(sv) || !SvMAGICAL(SvRV(sv)))
        return false;
    return mg_findext(SvRV(sv), PERL_MAGIC_ext, &Affix_pin_vtbl) != NULL;
}
void _pin_sv(pTHX_ SV * sv, const infix_type * type, void * pointer, bool managed) {
    if (SvREADONLY(sv))
        return;
    SvUPGRADE(sv, SVt_PVMG);
    MAGIC * mg = mg_findext(sv, PERL_MAGIC_ext, &Affix_pin_vtbl);
    Affix_Pin * pin;
    if (mg) {
        pin = (Affix_Pin *)mg->mg_ptr;
        if (pin && pin->managed && pin->pointer)
            safefree(pin->pointer);
        if (pin && pin->type_arena) {
            infix_arena_destroy(pin->type_arena);
            pin->type_arena = NULL;
        }
    }
    else {
        Newxz(pin, 1, Affix_Pin);
        mg = sv_magicext(sv, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
    }
    pin->pointer = pointer;
    pin->managed = managed;
    pin->type_arena = infix_arena_create(2048);
    if (!pin->type_arena) {
        safefree(pin);
        mg->mg_ptr = NULL;
        croak("Failed to create memory arena for pin's type information");
    }
    pin->type = _copy_type_graph_to_arena(pin->type_arena, type);
    if (!pin->type) {
        infix_arena_destroy(pin->type_arena);
        safefree(pin);
        mg->mg_ptr = NULL;
        croak("Failed to copy type information into pin");
    }
}
XS_INTERNAL(Affix_find_symbol) {
    dXSARGS;
    if (items != 2 || !sv_isobject(ST(0)) || !sv_derived_from(ST(0), "Affix::Lib"))
        croak_xs_usage(cv, "Affix_Lib_object, symbol_name");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    infix_library_t * lib = INT2PTR(infix_library_t *, tmp);
    const char * name = SvPV_nolen(ST(1));
    void * symbol = infix_library_get_symbol(lib, name);
    if (symbol) {
        Affix_Pin * pin;
        Newxz(pin, 1, Affix_Pin);
        pin->pointer = symbol;
        pin->managed = false;
        pin->type_arena = infix_arena_create(256);
        infix_type * void_ptr_type = NULL;
        if (infix_type_create_pointer_to(pin->type_arena, &void_ptr_type, infix_type_create_void()) != INFIX_SUCCESS) {
            safefree(pin);
            infix_arena_destroy(pin->type_arena);
            croak("Internal error: Failed to create pointer type for pin");
        }
        pin->type = void_ptr_type;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(pin));
        SV * rv = newRV_inc(obj_data);
        sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
        ST(0) = sv_2mortal(rv);
        XSRETURN(1);
    }
    XSRETURN_UNDEF;
}
XS_INTERNAL(Affix_pin) {
    dXSARGS;
    dMY_CXT;
    if (items != 4)
        croak_xs_usage(cv, "var, lib, symbol, type");
    SV * target_sv = ST(0);
    const char * lib_path_or_name = SvPV_nolen(ST(1));
    const char * symbol_name = SvPV_nolen(ST(2));
    const char * signature = SvPV_nolen(ST(3));
    infix_library_t * lib = infix_library_open(lib_path_or_name);
    if (lib == NULL)
        croak(
            "Failed to load library from path '%s' for pinning: %s", lib_path_or_name, infix_get_last_error().message);
    void * ptr = infix_library_get_symbol(lib, symbol_name);
    infix_library_close(lib);
    if (ptr == NULL)
        croak("Failed to locate symbol '%s' in library '%s'", symbol_name, lib_path_or_name);
    infix_type * type = NULL;
    infix_arena_t * arena = NULL;
    if (infix_type_from_signature(&type, &arena, signature, MY_CXT.registry) != INFIX_SUCCESS)
        croak_sv(_format_parse_error(aTHX_ "for pin", signature, infix_get_last_error()));
    _pin_sv(aTHX_ target_sv, type, ptr, false);
    infix_arena_destroy(arena);
    XSRETURN_YES;
}
XS_INTERNAL(Affix_unpin) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "var");
    if (mg_findext(ST(0), PERL_MAGIC_ext, &Affix_pin_vtbl) && !sv_unmagicext(ST(0), PERL_MAGIC_ext, &Affix_pin_vtbl))
        XSRETURN_YES;
    XSRETURN_NO;
}
XS_INTERNAL(Affix_sizeof) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "type_signature");
    const char * signature = SvPV_nolen(ST(0));
    infix_type * type = NULL;
    infix_arena_t * arena = NULL;
    if (infix_type_from_signature(&type, &arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (arena)
            infix_arena_destroy(arena);
        croak_sv(_format_parse_error(aTHX_ "for sizeof", signature, infix_get_last_error()));
    }
    size_t type_size = infix_type_get_size(type);
    infix_arena_destroy(arena);
    ST(0) = sv_2mortal(newSVuv(type_size));
    XSRETURN(1);
}
void _export_function(pTHX_ HV * _export, const char * what, const char * _tag) {
    SV ** tag = hv_fetch(_export, _tag, strlen(_tag), TRUE);
    if (tag && SvOK(*tag) && SvROK(*tag) && (SvTYPE(SvRV(*tag))) == SVt_PVAV)
        av_push((AV *)SvRV(*tag), newSVpv(what, 0));
    else {
        AV * av = newAV();
        av_push(av, newSVpv(what, 0));
        (void)hv_store(_export, _tag, strlen(_tag), newRV_noinc(MUTABLE_SV(av)), 0);
    }
}
void _affix_callback_handler_entry(infix_context_t * ctx, void * retval, void ** args) {
    Affix_Callback_Data * cb_data = (Affix_Callback_Data *)infix_reverse_get_user_data(ctx);
    if (!cb_data)
        return;
    dTHXa(cb_data->perl);
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    size_t num_args = infix_reverse_get_num_args(ctx);

    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * type = infix_reverse_get_arg_type(ctx, i);
        Affix_Pull puller = get_pull_handler(type);
        if (!puller)
            croak("Unsupported callback argument type");
        SV * arg_sv = newSV(0);
        puller(aTHX_ NULL, arg_sv, type, args[i]);
        mXPUSHs(arg_sv);
    }
    PUTBACK;
    const infix_type * ret_type = infix_reverse_get_return_type(ctx);
    U32 call_flags = G_EVAL | G_KEEPERR | ((ret_type->category == INFIX_TYPE_VOID) ? G_VOID : G_SCALAR);
    size_t count = call_sv(cb_data->coderef_rv, call_flags);
    if (SvTRUE(ERRSV)) {
        Perl_warn(aTHX_ "Perl callback died: %" SVf, ERRSV);
        sv_setsv(ERRSV, &PL_sv_undef);
        if (retval && !(call_flags & G_VOID))
            memset(retval, 0, infix_type_get_size(ret_type));
    }
    else if (call_flags & G_SCALAR) {
        SPAGAIN;
        SV * return_sv = (count == 1) ? POPs : &PL_sv_undef;
        sv2ptr(aTHX_ NULL, return_sv, retval, ret_type);
        PUTBACK;
    }
    FREETMPS;
    LEAVE;
}
XS_INTERNAL(Affix_as_string) {
    dVAR;
    dXSARGS;
    if (items < 1)
        croak_xs_usage(cv, "$affix");
    {
        char * RETVAL;
        dXSTARG;
        Affix * affix;
        if (sv_derived_from(ST(0), "Affix")) {
            IV tmp = SvIV((SV *)SvRV(ST(0)));
            affix = INT2PTR(Affix *, tmp);
        }
        else
            croak("affix is not of type Affix");
        RETVAL = (char *)affix->infix->target_fn;
        sv_setpv(TARG, RETVAL);
        XSprePUSH;
        PUSHTARG;
    }
    XSRETURN(1);
};
XS_INTERNAL(Affix_END) {
    dXSARGS;
    dMY_CXT;
    PERL_UNUSED_VAR(items);
    if (MY_CXT.lib_registry) {
        hv_iterinit(MY_CXT.lib_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.lib_registry))) {
            SV * entry_sv = HeVAL(he);
            LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
            if (entry) {
#if DEBUG > 0
                if (entry->ref_count > 0) {
                    warn("Affix: library handle for '%s' has %d outstanding references at END.",
                         HeKEY(he),
                         (int)entry->ref_count);
                }
#endif
                if (entry->lib)
                    infix_library_close(entry->lib);
                safefree(entry);
            }
        }
        hv_undef(MY_CXT.lib_registry);
        MY_CXT.lib_registry = NULL;
    }
    if (MY_CXT.callback_registry) {
        hv_iterinit(MY_CXT.callback_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.callback_registry))) {
            SV * entry_sv = HeVAL(he);
            Implicit_Callback_Magic * magic_data = INT2PTR(Implicit_Callback_Magic *, SvIV(entry_sv));
            if (magic_data) {
                infix_reverse_t * ctx = magic_data->reverse_ctx;
                if (ctx) {
                    Affix_Callback_Data * cb_data = (Affix_Callback_Data *)infix_reverse_get_user_data(ctx);
                    if (cb_data) {
                        SvREFCNT_dec(cb_data->coderef_rv);
                        safefree(cb_data);
                    }
                    infix_reverse_destroy(ctx);
                }
                safefree(magic_data);
            }
        }
        hv_undef(MY_CXT.callback_registry);
        MY_CXT.callback_registry = NULL;
    }
    if (MY_CXT.registry) {
        infix_registry_destroy(MY_CXT.registry);
        MY_CXT.registry = NULL;
    }
    XSRETURN_EMPTY;
}

XS_INTERNAL(Affix_typedef) {
    dXSARGS;
    dMY_CXT;
    if (items < 1 || items > 2)
        croak_xs_usage(cv, "$name, [$type]");

    SV * name_sv = ST(0);

    // Handle case where $name is an object (e.g., result of a previous typedef)
    // SvPV_nolen triggers stringification. If it's an Affix::Type, it returns "@Name".
    const char * raw_name = SvPV_nolen(name_sv);
    const char * name = raw_name;

    // Strip leading '@' if present to get the raw name "Cube" from "@Cube"
    if (name[0] == '@')
        name++;

    // 1. Construct the definition string for the registry
    // Format: "@Name = Type;" or "@Name;" (for forward declaration)
    SV * def_sv = sv_2mortal(newSVpvf("@%s", name));

    if (items == 2) {
        sv_catpv(def_sv, " = ");
        SV * type_sv = ST(1);
        // Handle both string signatures and Affix::Type objects for the type definition
        const char * type_str = _get_string_from_type_obj(aTHX_ type_sv);
        if (!type_str)
            type_str = SvPV_nolen(type_sv);
        sv_catpv(def_sv, type_str);
    }
    sv_catpv(def_sv, ";");

    // 2. Register the type definition
    // This updates the internal registry. If this is a re-definition (filling in a forward decl),
    // infix_register_types handles the update internally.
    if (infix_register_types(MY_CXT.registry, SvPV_nolen(def_sv)) != INFIX_SUCCESS)
        croak_sv(_format_parse_error(aTHX_ "in typedef", SvPV_nolen(def_sv), infix_get_last_error()));

    // 3. Install the constant subroutine in the caller's package.
    // To avoid "Constant subroutine redefined" warnings (and the confusing line number -1),
    // we check if the sub exists. If it does, we assume it's the correct one we created
    // in the forward declaration step, as its return value ("@Name") hasn't changed.
    HV * stash = CopSTASH(PL_curcop);
    bool sub_exists = false;

    if (stash) {
        // Look up the symbol in the stash
        SV ** entry = hv_fetch(stash, name, strlen(name), 0);
        if (entry && *entry && isGV(*entry)) {
            // If the glob contains a CV (subroutine), it exists.
            if (GvCV((GV *)*entry))
                sub_exists = true;
        }
    }

    if (!sub_exists) {
        // Install 'sub Name { return $rv }' in the caller's package
        SV * type_name_sv = newSVpvf("@%s", name);
        newCONSTSUB(stash, (char *)name, type_name_sv);
    }
    XSRETURN_YES;
}

/**
 * @brief XS function to return a list of all currently defined type names.
 * In list context, returns the names.
 * In scalar context, returns the count of names.
 */
XS_INTERNAL(Affix_defined_types) {
    dXSARGS;
    dMY_CXT;
    PERL_UNUSED_VAR(cv);

    // First pass: count the number of defined types.
    size_t count = 0;
    infix_registry_iterator_t it_counter = infix_registry_iterator_begin(MY_CXT.registry);
    while (infix_registry_iterator_next(&it_counter)) {
        // We only count types that are fully defined, not just forward-declared.
        if (infix_registry_iterator_get_type(&it_counter))
            count++;
    }

    if (GIMME_V == G_SCALAR) {
        ST(0) = sv_2mortal(newSVuv(count));
        XSRETURN(1);
    }
    if (count == 0)
        XSRETURN(0);

    EXTEND(SP, count);

    // Second pass: push the names onto the stack.
    infix_registry_iterator_t it = infix_registry_iterator_begin(MY_CXT.registry);
    while (infix_registry_iterator_next(&it)) {
        if (infix_registry_iterator_get_type(&it)) {
            const char * name = infix_registry_iterator_get_name(&it);
            PUSHs(sv_2mortal(newSVpv(name, 0)));
        }
    }
    XSRETURN(count);
}

void _DumpHex(pTHX_ const void * addr, size_t len, const char * file, int line) {
    if (addr == NULL) {
        printf("Dumping %lu bytes from null pointer %p at %s line %d\n", (unsigned long)len, addr, file, line);
        fflush(stdout);
        return;
    }
    fflush(stdout);
    int perLine = 16;
    if (perLine < 4 || perLine > 64)
        perLine = 16;
    size_t i;
    U8 * buff;
    Newxz(buff, perLine + 1, U8);
    const U8 * pc = (const U8 *)addr;
    printf("Dumping %lu bytes from %p at %s line %d\n", (unsigned long)len, addr, file, line);
    if (len == 0)
        croak("ZERO LENGTH");
    for (i = 0; i < len; i++) {
        if ((i % perLine) == 0) {
            if (i != 0)
                printf(" | %s\n", buff);
            printf("#  %03zu ", i);
        }
        printf(" %02x", pc[i]);
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % perLine] = '.';
        else
            buff[i % perLine] = pc[i];
        buff[(i % perLine) + 1] = '\0';
    }
    while ((i % perLine) != 0) {
        printf("   ");
        i++;
    }
    printf(" | %s\n", buff);
    safefree(buff);
    fflush(stdout);
}
void _DD(pTHX_ SV * scalar, const char * file, int line) {
    Perl_load_module(aTHX_ PERL_LOADMOD_NOIMPORT, newSVpvs("Data::Printer"), NULL, NULL, NULL);
    if (!get_cvs("Data::Printer::p", GV_NOADD_NOINIT | GV_NO_SVGMAGIC))
        return;
    fflush(stdout);
    dSP;
    int count;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    EXTEND(SP, 1);
    PUSHs(scalar);
    PUTBACK;
    count = call_pv("Data::Printer::p", G_SCALAR);
    SPAGAIN;
    if (count != 1)
        croak("Big trouble\n");
    STRLEN len;
    const char * s = SvPVx(POPs, len);
    printf("%s at %s line %d\n", s, file, line);
    fflush(stdout);
    PUTBACK;
    FREETMPS;
    LEAVE;
}
XS_INTERNAL(Affix_sv_dump) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "sv");
    sv_dump(ST(0));
    XSRETURN_EMPTY;
}
static SV * _new_pointer_obj(pTHX_ Affix_Pin * pin) {
    SV * data_sv = newSV(0);
    SV * rv = newRV_inc(data_sv);
    sv_setiv(data_sv, PTR2IV(pin));
    SvUPGRADE(data_sv, SVt_PVMG);
    sv_magicext(data_sv, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
    return rv;
}
XS_INTERNAL(Affix_malloc) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "size");
    UV size = SvUV(ST(0));
    infix_type * type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&type, &parse_arena, "*void", MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for malloc", "*void", infix_get_last_error()));
    }
    if (size == 0) {
        infix_arena_destroy(parse_arena);
        croak("Cannot malloc a zero-sized type");
    }
    void * ptr = safemalloc(size);
    Affix_Pin * pin;
    Newx(pin, 1, Affix_Pin);
    pin->size = size;
    pin->pointer = ptr;
    pin->managed = true;
    pin->type_arena = infix_arena_create(1024);
    pin->type = _copy_type_graph_to_arena(pin->type_arena, type);
    infix_arena_destroy(parse_arena);
    ST(0) = sv_2mortal(_new_pointer_obj(aTHX_ pin));
    XSRETURN(1);
}
XS_INTERNAL(Affix_calloc) {
    dXSARGS;
    dMY_CXT;
    if (items != 2)
        croak_xs_usage(cv, "count, type_signature");
    UV count = SvUV(ST(0));
    const char * signature = SvPV_nolen(ST(1));
    infix_type * elem_type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&elem_type, &parse_arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for calloc", signature, infix_get_last_error()));
    }
    size_t elem_size = infix_type_get_size(elem_type);
    if (elem_size == 0) {
        infix_arena_destroy(parse_arena);
        croak("Cannot calloc a zero-sized type");
    }
    void * ptr = safecalloc(count, elem_size);
    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);
    pin->pointer = ptr;
    pin->managed = true;
    pin->type_arena = infix_arena_create(1024);
    infix_type * array_type;
    if (infix_type_create_array(pin->type_arena, &array_type, elem_type, count) != INFIX_SUCCESS)
        croak("Failed to create array type graph.");
    pin->type = array_type;
    pin->size = (count * elem_size);
    infix_arena_destroy(parse_arena);
    ST(0) = sv_2mortal(_new_pointer_obj(aTHX_ pin));
    XSRETURN(1);
}
XS_INTERNAL(Affix_realloc) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "self, new_size");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin || !pin->managed)
        croak("Can only realloc a managed pointer");
    UV new_size = SvUV(ST(1));
    size_t old_size = pin->size;
    void * new_ptr = saferealloc(pin->pointer, new_size);
    if (new_size > old_size)
        memset((char *)new_ptr + old_size, 0, new_size - old_size);
    pin->pointer = new_ptr;
    pin->size = new_size;
    XSRETURN_YES;
}
XS_INTERNAL(Affix_free) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "pointer_object");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin) {
        warn("Affix::free called on a non-pointer object");
        XSRETURN_NO;
    }
    if (!pin->managed)
        croak("Cannot free a pointer that was not allocated by Affix (it is unmanaged)");
    if (pin->pointer) {
        safefree(pin->pointer);
        pin->pointer = NULL;
    }
    XSRETURN_YES;
}
XS_INTERNAL(Affix_cast) {
    dXSARGS;
    dMY_CXT;
    if (items != 2)
        croak_xs_usage(cv, "self, new_type_signature");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin)
        croak("Argument is not a pointer");
    const char * signature = SvPV_nolen(ST(1));
    infix_type * new_type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&new_type, &parse_arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for cast", signature, infix_get_last_error()));
    }
    if (pin->type_arena)
        infix_arena_destroy(pin->type_arena);
    pin->type_arena = infix_arena_create(1024);
    pin->type = _copy_type_graph_to_arena(pin->type_arena, new_type);
    infix_arena_destroy(parse_arena);
    ST(0) = ST(0);
    XSRETURN(1);
}
XS_INTERNAL(Affix_dump) {
    dVAR;
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "self, length_in_bytes");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin)
        croak("self is not a valid pointer");
    if (!pin->pointer) {
        warn("Cannot dump a NULL pointer");
        XSRETURN_EMPTY;
    }
    UV length = SvUV(ST(1));
    if (length == 0) {
        warn("Dump length cannot be zero");
        XSRETURN_EMPTY;
    }
    _DumpHex(aTHX_ pin->pointer, length, OutCopFILE(PL_curcop), CopLINE(PL_curcop));
    ST(0) = ST(0);
    XSRETURN(1);
}
void _populate_hv_from_c_struct(pTHX_ Affix * affix, HV * hv, const infix_type * type, void * p) {
    hv_clear(hv);
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (member->name) {
            void * member_ptr = (char *)p + member->offset;
            SV * member_sv = newSV(0);
            ptr2sv(aTHX_ affix, member_ptr, member_sv, member->type);
            hv_store(hv, member->name, strlen(member->name), member_sv, 0);
        }
    }
}

void boot_Affix(pTHX_ CV * cv) {
    dVAR;
    dXSBOOTARGSXSAPIVERCHK;
    PERL_UNUSED_VAR(items);
#ifdef USE_ITHREADS
    my_perl = (PerlInterpreter *)PERL_GET_CONTEXT;
#endif
    MY_CXT_INIT;
    MY_CXT.lib_registry = newHV();
    MY_CXT.callback_registry = newHV();
    MY_CXT.registry = infix_registry_create();
    if (!MY_CXT.registry)
        croak("Failed to initialize the global type registry");
    {
        cv = newXSproto_portable("Affix::affix", Affix_affix, __FILE__, "$$$;$");
        XSANY.any_i32 = 0;
        export_function("Affix", "affix", "base");
        cv = newXSproto_portable("Affix::wrap", Affix_affix, __FILE__, "$$$;$");
        XSANY.any_i32 = 1;
        export_function("Affix", "wrap", "base");
        newXS("Affix::DESTROY", Affix_DESTROY, __FILE__);
    }
    {
        cv = newXSproto_portable("Affix::direct_affix", Affix_affix, __FILE__, "$$$;$");
        XSANY.any_i32 = 2;
        export_function("Affix", "direct_affix", "base");
        cv = newXSproto_portable("Affix::direct_wrap", Affix_affix, __FILE__, "$$$;$");
        XSANY.any_i32 = 3;
        export_function("Affix", "direct_wrap", "base");
        newXS("Affix::Bundled::DESTROY", Affix_Bundled_DESTROY, __FILE__);
    }

    newXS("Affix::END", Affix_END, __FILE__);
    sv_setsv(get_sv("Affix::()", TRUE), &PL_sv_yes);
    (void)newXSproto_portable("Affix::()", Affix_as_string, __FILE__, "$;@");
    newXS("Affix::load_library", Affix_load_library, __FILE__);
    sv_setsv(get_sv("Affix::Lib::()", TRUE), &PL_sv_yes);
    (void)newXSproto_portable("Affix::Lib::(0+", Affix_Lib_as_string, __FILE__, "$;@");
    (void)newXSproto_portable("Affix::Lib::()", Affix_as_string, __FILE__, "$;@");
    newXS("Affix::Lib::DESTROY", Affix_Lib_DESTROY, __FILE__);
    newXS("Affix::find_symbol", Affix_find_symbol, __FILE__);
    newXS("Affix::get_last_error_message", Affix_get_last_error_message, __FILE__);
    (void)newXSproto_portable("Affix::pin", Affix_pin, __FILE__, "$$$$");
    export_function("Affix", "pin", "pin");
    (void)newXSproto_portable("Affix::unpin", Affix_unpin, __FILE__, "$");
    export_function("Affix", "unpin", "pin");
    (void)newXSproto_portable("Affix::sizeof", Affix_sizeof, __FILE__, "$");
    export_function("Affix", "sizeof", "core");

    (void)newXSproto_portable("Affix::typedef", Affix_typedef, __FILE__, "$;$");
    (void)newXSproto_portable("Affix::types", Affix_defined_types, __FILE__, "");
    export_function("Affix", "typedef", "registry");

    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");
    export_function("Affix", "load_library", "lib");
    export_function("Affix", "find_symbol", "lib");
    export_function("Affix", "get_last_error_message", "core");
    (void)newXSproto_portable("Affix::sv_dump", Affix_sv_dump, __FILE__, "$");
    {
        (void)newXSproto_portable("Affix::malloc", Affix_malloc, __FILE__, "$");
        (void)newXSproto_portable("Affix::calloc", Affix_calloc, __FILE__, "$$");
        (void)newXSproto_portable("Affix::realloc", Affix_realloc, __FILE__, "$$");
        (void)newXSproto_portable("Affix::free", Affix_free, __FILE__, "$");
        (void)newXSproto_portable("Affix::cast", Affix_cast, __FILE__, "$$");
        (void)newXSproto_portable("Affix::dump", Affix_dump, __FILE__, "$$");
        export_function("Affix", "malloc", "mem");
        export_function("Affix", "calloc", "mem");
        export_function("Affix", "free", "mem");
    }
    Perl_xs_boot_epilog(aTHX_ ax);
}
