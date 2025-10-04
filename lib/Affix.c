#include "Affix.h"
#include <string.h>


// Include the entire infix library
#include "../../infix/src/infix.c"

// Include all other Affix module C files
#include "Affix/Callback.c"
#include "Affix/Platform.c"
#include "Affix/Pointer.c"
#include "Affix/marshal.c"
#include "Affix/pin.c"
#include "Affix/utils.c"
#include "Affix/wchar_t.c"


// CORRECTED: A simple global static pointer is sufficient and safer for a
// single-threaded environment, as requested.
Affix_Context * current_call_context = NULL;


#if defined(INFIX_OS_WINDOWS)
DLLib load_library(const char * lib) {
    return LoadLibraryA(lib);
}
void * find_symbol(DLLib lib, const char * name) {
    return (void *)GetProcAddress(lib, name);
}
void free_library(DLLib lib) {
    if (lib)
        FreeLibrary(lib);
}
const char * get_dl_error() {
    static char buf[1024];
    DWORD dw = GetLastError();
    if (dw == 0)
        return NULL;
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, 0, (LPSTR)buf, sizeof(buf), NULL);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return buf;
}
#else
DLLib load_library(const char * lib) {
    return dlopen(lib, RTLD_LAZY | RTLD_GLOBAL);
}
void * find_symbol(DLLib lib, const char * name) {
    return dlsym(lib, name);
}
void free_library(DLLib lib) {
    if (lib)
        dlclose(lib);
}
const char * get_dl_error() {
    return dlerror();
}
#endif

// --- XSUBs for Library Loading ---
XS_INTERNAL(Affix_dlerror) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    const char * ret = get_dl_error();
    ST(0) = sv_2mortal(newSVpv(ret ? ret : "", 0));
    XSRETURN(1);
}
XS_INTERNAL(Affix_load_library) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$lib");
    DLLib lib = SvOK(ST(0)) ? load_library(SvPV_nolen(ST(0))) : load_library(NULL);
    if (!lib)
        XSRETURN_UNDEF;
    ST(0) = sv_setref_pv(sv_newmortal(), "Affix::Lib", lib);
    XSRETURN(1);
}
XS_INTERNAL(Affix_find_symbol) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$lib, $symbol");
    DLLib lib = NULL;
    if (sv_isobject(ST(0)) && sv_derived_from(ST(0), "Affix::Lib"))
        lib = INT2PTR(DLLib, SvIV(SvRV(ST(0))));

    else if (SvPOK(ST(0)))
        lib = load_library(SvPV_nolen(ST(0)));
    else
        croak("First argument must be an Affix::Lib object or a library path");
    if (!lib)
        XSRETURN_UNDEF;
    void * lib_handle = find_symbol(lib, SvPV_nolen(ST(1)));
    if (!lib_handle)
        XSRETURN_UNDEF;
    ST(0) = newSViv(PTR2IV(lib_handle));
    XSRETURN(1);
}
XS_INTERNAL(Affix_free_library) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "lib");
    if (sv_isobject(ST(0)) && sv_derived_from(ST(0), "Affix::Lib")) {
        DLLib lib = INT2PTR(DLLib, SvIV(SvRV(ST(0))));
        free_library(lib);
        sv_setiv(SvRV(ST(0)), 0);
    }
    else
        croak("lib is not of type Affix::Lib");
    XSRETURN_EMPTY;
}

void destroy_affix(pTHX_ Affix_Context * context) {
    if (context == NULL)
        return;

    if (context->owned_callbacks) {
        SSize_t len = av_len(context->owned_callbacks);
        for (SSize_t i = 0; i <= len; ++i) {
            SV** elem = av_fetch(context->owned_callbacks, i, 0);
            if (elem && SvIOK(*elem)) {
                infix_reverse_t* rt = INT2PTR(infix_reverse_t*, SvIV(*elem));
                if (rt) {
                    Affix_Callback_Data* user_data = infix_reverse_get_user_data(rt);
                    if(user_data) {
                        SvREFCNT_dec(user_data->perl_sub);
                        safefree(user_data);
                    }
                    infix_reverse_destroy(rt);
                }
            }
        }
        SvREFCNT_dec((SV*)context->owned_callbacks);
    }

    infix_forward_destroy(context->trampoline);
    safefree(context);
}

extern void _Affix_trigger_infix(pTHX_ CV * cv) {
    dXSARGS;

    Affix_Context * context = (Affix_Context *)XSANY.any_ptr;
    Affix affix = context->trampoline;
    size_t num_args = infix_forward_get_num_args(affix);

    if (UNLIKELY(items != num_args))
        croak("Wrong number of arguments to affixed function: expected %" UVuf ", got %d", (UV)num_args, (int)items);

    infix_cif_func jit_func = (infix_cif_func)infix_forward_get_code(affix);
    if (UNLIKELY(!jit_func))
        croak("Internal Affix Error: JIT trampoline is not executable.");

    current_call_context = context;

    void ** args_array = safemalloc(num_args * sizeof(void *));
    size_t total_arg_data_size = 0;
    for (size_t i = 0; i < num_args; ++i) {
        const infix_type* arg_type = infix_forward_get_arg_type(affix, i);
        size_t alignment = arg_type->alignment > 0 ? arg_type->alignment : 1;
        total_arg_data_size = _infix_align_up(total_arg_data_size, alignment);
        total_arg_data_size += arg_type->size;
    }
    char * c_args_buffer = safemalloc(total_arg_data_size);
    char * current_arg_ptr = c_args_buffer;

    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * arg_type = infix_forward_get_arg_type(affix, i);
        size_t alignment = arg_type->alignment > 0 ? arg_type->alignment : 1;
        current_arg_ptr = (char *)_infix_align_up((uintptr_t)current_arg_ptr, alignment);
        marshal_sv_to_c(aTHX_ current_arg_ptr, ST(i), arg_type);
        args_array[i] = current_arg_ptr;
        current_arg_ptr += arg_type->size;
    }

    const infix_type* ret_type = infix_forward_get_return_type(affix);
    void* return_buffer = NULL;
    void* aligned_return_buffer = NULL;

    if (ret_type->category != INFIX_TYPE_VOID) {
        size_t ret_size = ret_type->size;
        size_t ret_align = ret_type->alignment > 0 ? ret_type->alignment : 1;
        return_buffer = safemalloc(ret_size + ret_align);
        aligned_return_buffer = (void*)_infix_align_up((uintptr_t)return_buffer, ret_align);
    }

    jit_func(context->symbol, aligned_return_buffer, args_array);

    current_call_context = NULL;
    safefree(args_array);
    safefree(c_args_buffer);

    if (ret_type->category == INFIX_TYPE_VOID) {
        if (return_buffer) safefree(return_buffer);
        XSRETURN_EMPTY;
    }
    else {
        ST(0) = ptr2sv(aTHX_ aligned_return_buffer, ret_type);
        if (return_buffer) safefree(return_buffer);
        XSRETURN(1);
    }
}


XS_INTERNAL(Affix_affix) {
    dXSARGS;
    dXSI32;
    if (items < 3 || items > 4)
        croak_xs_usage(cv, "$lib, $symbol, $signature[, $rename]");
    DLLib libhandle = SvOK(ST(0)) ? load_library(SvPV_nolen(ST(0))) : load_library(NULL);
    if (!libhandle)
        croak("Failed to load library '%s': %s", SvPV_nolen(ST(0)), get_dl_error());
    void * funcptr = find_symbol(libhandle, SvPV_nolen(ST(1)));
    if (!funcptr)
        croak("Failed to find symbol '%s' in library '%s': %s", SvPV_nolen(ST(1)), SvPV_nolen(ST(0)), get_dl_error());
    const char * signature = SvPV_nolen(ST(2));
    const char * rename = items == 4 && SvOK(ST(3)) ? SvPV_nolen(ST(3)) : NULL;
    const char * install_name = (ix == 0) ? (rename ? rename : SvPV_nolen(ST(1))) : NULL;

    Affix_Context * context;
    Newxz(context, 1, Affix_Context);
    context->symbol = funcptr;
    context->owned_callbacks = newAV();

    infix_arena_t * parse_arena = NULL;
    infix_type * return_type = NULL;
    infix_function_argument * args = NULL;
    size_t num_args = 0;
    size_t num_fixed_args = 0;

    infix_status status =
        infix_signature_parse(signature, &parse_arena, &return_type, &args, &num_args, &num_fixed_args);
    if (status != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        safefree(context);
        croak("Failed to parse FFI signature string: '%s' [%d != %d]", signature, status, INFIX_SUCCESS);
    }

    infix_type ** arg_types = NULL;
    if (num_args > 0) {
        arg_types = infix_arena_alloc(parse_arena, sizeof(infix_type *) * num_args, _Alignof(infix_type *));
        for (size_t i = 0; i < num_args; i++)
            arg_types[i] = args[i].type;
    }

    status = infix_forward_create_manual(&context->trampoline, return_type, arg_types, num_args, num_fixed_args);

    if (parse_arena)
        infix_arena_destroy(parse_arena);

    if (status != INFIX_SUCCESS) {
        destroy_affix(aTHX_ context);
        croak("Failed to generate FFI trampoline for signature: '%s' [%d]", signature, status);
    }

    char * prototype = (char *)safemalloc(num_args + 1);
    memset(prototype, '$', num_args);
    prototype[num_args] = '\0';

    STMT_START {
        cv = newXSproto_portable(install_name, _Affix_trigger_infix, __FILE__, prototype);
        safefree(prototype);
        if (!cv) {
            destroy_affix(aTHX_ context);
            croak("Failed to create new XSUB");
        }
        XSANY.any_ptr = (void *)context;
    }
    STMT_END;

    SV * ret_sv = sv_bless(newRV_inc(MUTABLE_SV(cv)), gv_stashpv("Affix", GV_ADD));
    ST(0) = (ix == 1) ? newRV_inc(MUTABLE_SV(cv)) : ret_sv;
    XSRETURN(1);
}

XS_INTERNAL(Affix_DESTROY) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    Affix_Context * context;
    STMT_START {  // peel this grape
        HV * st;
        GV * gvp;
        SV * const xsub_tmp_sv = ST(0);
        SvGETMAGIC(xsub_tmp_sv);
        CV * cv = sv_2cv(xsub_tmp_sv, &st, &gvp, 0);
        context = (Affix_Context *)XSANY.any_ptr;
    }
    STMT_END;
    destroy_affix(aTHX_ context);
    XSRETURN_EMPTY;
}

// --- XS Boot Section ---
void boot_Affix(pTHX_ CV * cv) {
    PERL_UNUSED_VAR(cv);
    dXSBOOTARGSXSAPIVERCHK;

    (void)newXSproto_portable("Affix::affix", Affix_affix, __FILE__, "$$$;$");
    XSANY.any_i32 = 0;
    (void)newXSproto_portable("Affix::wrap", Affix_affix, __FILE__, "$$$$");
    XSANY.any_i32 = 1;
    (void)newXSproto_portable("Affix::DESTROY", Affix_DESTROY, __FILE__, "$");

    (void)newXSproto_portable("Affix::dl::load_library", Affix_load_library, __FILE__, "$");
    (void)newXSproto_portable("Affix::dl::free_library", Affix_free_library, __FILE__, "$");
    (void)newXSproto_portable("Affix::dl::find_symbol", Affix_find_symbol, __FILE__, "$$");
    (void)newXSproto_portable("Affix::dl::dlerror", Affix_dlerror, __FILE__, "");

    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");

    boot_Affix_Platform(aTHX_ cv);
    boot_Affix_Pointer(aTHX_ cv);
    boot_Affix_pin(aTHX_ cv);
    boot_Affix_Callback(aTHX_ cv);
    Perl_xs_boot_epilog(aTHX_ ax);
}
