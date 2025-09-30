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
 * @param context A pointer to the `ffi_reverse_trampoline_t` context. We
 *                retrieve our own `Affix_CallbackCtx` from its `user_data`.
 * @param return_value_ptr A pointer to a C buffer for the return value.
 * @param args_array An array of `void*` pointers to the C arguments.
 */
void _Affix_callback_handler(ffi_reverse_trampoline_t * context, void * return_value_ptr, void ** args_array) {
    warn("In handler. ret ptr: %p", return_value_ptr);
    if (context == NULL)
        croak("null");
    else
        warn("not null");
    //~ void * blah = ffi_reverse_trampoline_get_user_data(context);
    //~ if (blah != NULL)
    //~ croak("hi");
    //~ DumpHex(blah, 16);
    // 1. Retrieve our context and set the interpreter context for this thread.
    Affix_Callback * ctx = (Affix_Callback *)ffi_reverse_trampoline_get_user_data(context);
    warn("Line: %d, File: %s", __LINE__, __FILE__);
    if (!ctx->perl)
        croak("Fuck");
    dTHXa(ctx->perl);
    sv_dump(newSV(0));
    warn("Line: %d, File: %s", __LINE__, __FILE__);
    dSP;
    int count;
    warn("Line: %d, File: %s", __LINE__, __FILE__);

    // 2. Set up the Perl call stack.
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    EXTEND(SP, ctx->num_args);
    warn("Line: %d, File: %s", __LINE__, __FILE__);

    // 3. Marshal C arguments to Perl SVs and push them onto the stack.
    for (size_t i = 0; i < ctx->num_args; i++) {
        SV * arg_sv = fetch_c_to_sv(aTHX_ args_array[i], ctx->arg_types[i]);
        sv_dump(arg_sv);
        mPUSHs((arg_sv));
    }
    PUTBACK;
    warn("Line: %d, File: %s", __LINE__, __FILE__);

    // 4. Call the Perl subroutine.
    count = call_sv(ctx->perl_sub, G_SCALAR);
    SPAGAIN;
    warn("Line: %d, File: %s", __LINE__, __FILE__);

    // 5. Marshal the return value from Perl back to the C buffer.
    if (ctx->return_type->category != FFI_TYPE_VOID) {
        warn("Line: %d, File: %s", __LINE__, __FILE__);

        if (count != 1) {
            warn("Line: %d, File: %s", __LINE__, __FILE__);
            //~ Newxz(return_value_ptr, 1, SV);
            // If Perl returns an empty list, we return a zeroed-out value.
            memset(return_value_ptr, 0, ctx->return_type->size);
        }
        else {
            warn("Line: %d, File: %s", __LINE__, __FILE__);

            SV * return_sv = POPs;
            warn("Line: %d, File: %s", __LINE__, __FILE__);

            marshal_sv_to_c(aTHX_ return_value_ptr, return_sv, ctx->return_type);
            DumpHex(return_value_ptr, 16);
            DumpHex(*(void **)return_value_ptr, 16);
            warn("Line: %d, File: %s", __LINE__, __FILE__);
        }
    }
    warn("Line: %d, File: %s", __LINE__, __FILE__);

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

    // 1. Parse the callback's signature.
    arena_t * arena = NULL;
    ffi_type * ret_type = NULL;
    ffi_type ** arg_types = NULL;
    size_t num_args = 0;
    size_t num_fixed_args = 0;  // TODO: Support variadic callbacks later

    ffi_status status = ffi_signature_parse(signature, &arena, &ret_type, &arg_types, &num_args, &num_fixed_args);
    if (status != FFI_SUCCESS) {
        if (arena)
            arena_destroy(arena);
        croak("Failed to parse callback signature: '%s'", signature);
    }

    // 2. Create our persistent context struct.
    Affix_Callback * ctx;
    Newxz(ctx, 1, Affix_Callback);
    storeTHX(ctx->perl);
    ctx->perl_sub = newSVsv(sub);  // Increment refcount
    ctx->return_type = ret_type;
    ctx->arg_types = arg_types;
    ctx->num_args = num_args;

    // 3. Generate the reverse trampoline.
    ffi_reverse_trampoline_t * rt = NULL;
    warn("SETTING UP CALLBACK!!!!!!!!!!! line %d", __LINE__);
    status = generate_reverse_trampoline(
        &rt, ret_type, arg_types, num_args, num_fixed_args, (void *)_Affix_callback_handler, ctx);

    // The arena is now owned by the trampoline context, we don't destroy it here.

    if (status != FFI_SUCCESS) {
        SvREFCNT_dec(ctx->perl_sub);
        safefree(ctx);
        croak("Failed to generate reverse trampoline for signature: '%s'", signature);
    }

    // 4. Wrap the resulting native function pointer in an Affix::Pointer object.
    void * func_ptr = ffi_trampoline_get_code((ffi_forward_t *)rt);  // HACK: cast is okay here

    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = func_ptr;
    ptr_struct->managed = 0;                       // The trampoline owns this memory, not us.
    ptr_struct->type = ffi_type_create_pointer();  // Type is just a generic pointer
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
