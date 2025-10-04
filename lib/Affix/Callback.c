/**
 * @file Callback.c
 * @brief Implements the reverse call (callback) functionality for Affix.
 *
 * @details This file provides the `callback()` XSUB, which is the entry point
 * for creating native C function pointers from Perl subroutines.
 *
 * It uses the `infix` library's reverse trampoline feature to JIT-compile a
 * native C stub for each callback. When this stub is called from C, it invokes
 * a generic C handler (`_Affix_callback_handler`) which then marshals the
 * arguments from C to Perl, calls the original Perl subroutine, and marshals
 * the return value from Perl back to C.
 */

#include "../Affix.h"

/**
 * @brief The generic C handler for all infix reverse trampolines.
 *
 * @details This is the C function that the JIT-compiled stub calls. Its job is
 *          to bridge the gap between the native C call and the Perl world.
 *
 * @param context A pointer to the `infix_context_t` context. We
 *                retrieve our own `Affix_Callback_Data` from its `user_data`.
 * @param return_value_ptr A pointer to a C buffer for the return value.
 * @param args_array An array of `void*` pointers to the C arguments.
 */
void _Affix_callback_handler(infix_context_t * context, void * return_value_ptr, void ** args_array) {
    // 1. Retrieve our context and set the interpreter context for this thread.
    Affix_Callback_Data * ctx = (Affix_Callback_Data *)infix_reverse_get_user_data(context);
    if (!ctx || !ctx->perl) {
        if (return_value_ptr) {
            const infix_type* ret_type = infix_reverse_get_return_type(context);
            if(ret_type) memset(return_value_ptr, 0, ret_type->size);
        }
        return;
    }

    dTHXa(ctx->perl);
    dSP;
    int count;

    // 2. Set up the Perl call stack.
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);

    // 3. Marshal C arguments to Perl SVs and push them onto the stack.
    size_t num_args = infix_reverse_get_num_args(context);
    EXTEND(SP, num_args);

    for (size_t i = 0; i < num_args; i++) {
        const infix_type * type_info = infix_reverse_get_arg_type(context, i);
        SV * arg_sv = ptr2sv(aTHX_ args_array[i], type_info);
        PUSHs(arg_sv); // ptr2sv now returns a mortal SV
    }
    PUTBACK;

    // 4. Call the Perl subroutine.
    count = call_sv(ctx->perl_sub, G_SCALAR);
    SPAGAIN;

    // 5. Marshal the return value from Perl back to the C buffer.
    const infix_type* return_type = infix_reverse_get_return_type(context);
    if (return_type->category != INFIX_TYPE_VOID) {
        if (count != 1) {
            // If Perl returns an empty list, we return a zeroed-out value.
            memset(return_value_ptr, 0, return_type->size);
        } else {
            SV * return_sv = POPs;
            marshal_sv_to_c(aTHX_ return_value_ptr, return_sv, return_type);
        }
    }

    PUTBACK;
    FREETMPS;
    LEAVE;
}

/**
 * @brief XSUB for `Affix::callback($sub, $signature)`.
 * @return A new, unmanaged `Affix::Pointer` containing the native function pointer.
 */
XS_INTERNAL(Affix_callback) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$coderef, $signature");

    SV * sub = ST(0);
    if (!SvROK(sub) || SvTYPE(SvRV(sub)) != SVt_PVCV) {
        croak("First argument to callback() must be a code reference");
    }
    const char * signature = SvPV_nolen(ST(1));

    // 1. Parse the callback's signature into a temporary arena.
    infix_arena_t * arena = NULL;
    infix_type * ret_type = NULL;
    infix_function_argument * args = NULL;
    size_t num_args = 0;
    size_t num_fixed_args = 0;

    infix_status status = infix_signature_parse(signature, &arena, &ret_type, &args, &num_args, &num_fixed_args);
    if (status != INFIX_SUCCESS) {
        if (arena)
            infix_arena_destroy(arena);
        croak("Failed to parse callback signature: '%s'", signature);
    }

    // Extract just the types from the parsed arguments
    infix_type ** arg_types = infix_arena_alloc(arena, sizeof(infix_type *) * num_args, _Alignof(infix_type *));
    if (num_args > 0 && !arg_types) {
        infix_arena_destroy(arena);
        croak("Failed to allocate memory for argument types");
    }
    for (size_t i = 0; i < num_args; ++i) {
        arg_types[i] = args[i].type; // CORRECTED: Changed -> to .
    }

    // 2. Create our persistent user context.
    Affix_Callback_Data * ctx;
    Newxz(ctx, 1, Affix_Callback_Data);
    storeTHX(ctx->perl);
    ctx->perl_sub = newSVsv(sub);  // Increment refcount

    // 3. Generate the reverse trampoline. It will deep-copy the types.
    infix_reverse_t * rt = NULL;
    status = infix_reverse_create_manual(
        &rt, ret_type, arg_types, num_args, num_fixed_args, (void *)_Affix_callback_handler, ctx);

    // The temporary parsing arena is no longer needed. The reverse trampoline
    // has its own self-contained copy of the type info.
    infix_arena_destroy(arena);

    if (status != INFIX_SUCCESS) {
        SvREFCNT_dec(ctx->perl_sub);
        // arg_types was in the destroyed arena
        safefree(ctx);
        croak("Failed to generate reverse trampoline for signature: '%s'", signature);
    }

    // 4. Wrap the resulting native function pointer in an Affix::Pointer object.
    void * func_ptr = infix_reverse_get_code(rt);

    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = func_ptr;
    ptr_struct->managed = 0; // The reverse trampoline context owns this memory.
    ptr_struct->type = (infix_type *)infix_type_create_pointer();
    ptr_struct->type_arena = NULL;
    ptr_struct->count = 1;
    ptr_struct->position = 0;

    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer::Unmanaged", GV_ADD));
    XSRETURN(1);
}

void boot_Affix_Callback(pTHX_ CV * cv) {
    PERL_UNUSED_VAR(cv);
    (void)newXSproto_portable("Affix::callback", Affix_callback, __FILE__, "$$");
    export_function("Affix", "callback", "core");
}
