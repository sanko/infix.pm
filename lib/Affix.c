#include "Affix.h"
#include <string.h>

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//               FORWARD DECLARATIONS FOR STATIC FUNCTIONS
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// --- Pin Management ---
static int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg);
static int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg);
static U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg);
static int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg);

// --- Execution Plan Step Executors ---
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

// --- sv2ptr Primitive Push Handlers ---
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

// --- Pull handlers (C -> Perl) ---
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
DEFINE_PUSH_HANDLER(double, double, SvNOK, SvNV)
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

void Affix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;
    //~ if (!affix)
    //~ croak("Internal error: Affix context is NULL in trigger");

    if ((SP - MARK) != affix->num_args)
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)affix->num_args,
              (UV)(SP - MARK));

    SV ** perl_stack_frame = &ST(0);

    affix->args_arena->current_offset = 0;
    affix->ret_arena->current_offset = 0;

    void * args_buffer = infix_arena_alloc(affix->args_arena, affix->total_args_size, 16);
    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    void * ret_buffer =
        infix_arena_alloc(affix->ret_arena, infix_type_get_size(ret_type), infix_type_get_alignment(ret_type));

    void * c_args[affix->num_args ? affix->num_args : 1];

    for (size_t i = 0; i < affix->plan_length; ++i)
        affix->plan[i].executor(aTHX_ affix, &affix->plan[i], perl_stack_frame, args_buffer, c_args, ret_buffer);

    //~ if (affix->num_out_params > 0) {
    for (size_t i = 0; i < affix->num_out_params; ++i) {
        const OutParamInfo * info = &affix->out_param_info[i];
        SV * arg_sv = perl_stack_frame[info->perl_stack_index];
        if (SvROK(arg_sv) && !is_pin(aTHX_ arg_sv)) {
            SV * rsv = SvRV(arg_sv);
            if (SvTYPE(rsv) == SVt_PVAV)
                continue;
            info->writer(aTHX_ affix, info, rsv, c_args[info->perl_stack_index]);
        }
    }
    //~ }

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
    if (items != 3)
        croak_xs_usage(cv, "Affix::affix($target, $name_spec, $signature)");
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
    Affix * affix;
    Newxz(affix, 1, Affix);
    affix->return_sv = newSV(0);
    if (created_implicit_handle)
        affix->lib_handle = lib_handle_for_symbol;
    else
        affix->lib_handle = NULL;
    const char * signature = SvPV_nolen(ST(2));
    infix_status status = infix_forward_create(&affix->infix, signature, symbol, MY_CXT.registry);
    if (status != INFIX_SUCCESS) {
        SvREFCNT_dec(affix->return_sv);
        safefree(affix);
        croak("Failed to parse signature or create trampoline: %s", infix_get_last_error().message);
    }
    affix->cif = infix_forward_get_code(affix->infix);
    affix->num_args = infix_forward_get_num_args(affix->infix);
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
    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    affix->plan[affix->num_args + 1].executor = plan_step_pull_return_value;
    affix->plan[affix->num_args + 1].data.type = ret_type;
    affix->plan[affix->num_args + 1].data.pull_handler = get_pull_handler(ret_type);
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
        sv_setsv(sv, newRV_noinc(MUTABLE_SV(hv)));
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
        sv_setsv(sv, newRV_noinc(MUTABLE_SV(av)));
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
        sv_setsv(sv, newRV_noinc(MUTABLE_SV(av)));
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
        sv_setsv(sv, newRV_noinc(MUTABLE_SV(av)));
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
static void pull_pointer(pTHX_ Affix * affix, SV * sv, const infix_type * type, void * ptr) {
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL) {
        sv_setsv(sv, &PL_sv_undef);
        return;
    }
    const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
    switch (pointee_type->category) {
    case INFIX_TYPE_STRUCT:
        pull_struct(aTHX_ affix, sv, pointee_type, c_ptr);
        return;
    case INFIX_TYPE_ARRAY:
        pull_array(aTHX_ affix, sv, pointee_type, c_ptr);
        return;
    case INFIX_TYPE_PRIMITIVE:
        if (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
            pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8) {
            sv_setpv(sv, (const char *)c_ptr);
            return;
        }
    default:
        break;
    }
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ sv);
    if (!pin) {
        Newxz(pin, 1, Affix_Pin);
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(pin));
        SV * rv = newRV_noinc(obj_data);
        sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
        sv_setsv(sv, rv);
        sv_bless(sv, gv_stashpv("Affix::Pin", GV_ADD));
    }
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
            if (name && strnEQ(name, "SV", 2))
                return pull_sv;
            return pull_pointer;
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
    if (!pin->type) {
        Perl_warn(aTHX_ "Affix internal warning: pin has no type information in get_pin");
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }
    if (pin->type->category == INFIX_TYPE_POINTER) {
        const infix_type * pointee_type = pin->type->meta.pointer_info.pointee_type;
        if (pointee_type->category == INFIX_TYPE_PRIMITIVE &&
            (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
             pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
            sv_setpv(sv, (const char *)pin->pointer);
        }
        else if (pointee_type->category == INFIX_TYPE_VOID) {
            if (pin->size > 0)
                sv_setpvn(sv, pin->pointer, pin->size);
            else
                sv_setuv(sv, PTR2UV(pin->pointer));
        }
        else
            ptr2sv(aTHX_ NULL, pin->pointer, sv, pointee_type);
    }
    else
        ptr2sv(aTHX_ NULL, pin->pointer, sv, pin->type);
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
    if (items != 1)
        croak_xs_usage(cv, "types_string");
    const char * types = SvPV_nolen(ST(0));
    if (infix_register_types(MY_CXT.registry, types) != INFIX_SUCCESS)
        croak_sv(_format_parse_error(aTHX_ "in typedef", types, infix_get_last_error()));
    XSRETURN_YES;
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
    cv = newXSproto_portable("Affix::affix", Affix_affix, __FILE__, "$$$");
    XSANY.any_i32 = 0;
    export_function("Affix", "affix", "base");
    cv = newXSproto_portable("Affix::wrap", Affix_affix, __FILE__, "$$$");
    XSANY.any_i32 = 1;
    export_function("Affix", "wrap", "base");
    newXS("Affix::DESTROY", Affix_DESTROY, __FILE__);
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
    (void)newXSproto_portable("Affix::typedef", Affix_typedef, __FILE__, "$");
    export_function("Affix", "sizeof", "core");
    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");
    export_function("Affix", "load_library", "lib");
    export_function("Affix", "find_symbol", "lib");
    export_function("Affix", "get_last_error_message", "core");
    export_function("Affix", "typedef", "registry");
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
