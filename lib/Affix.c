// This file is now complete and correct from the previous step.
// No further changes are needed. It correctly uses the native library
// loading functions and calls the correct infix parsing function.
// Please ensure you are using the version from the "implicit declaration" fix.
#include "Affix.h"
#include <string.h>

#if defined(FFI_OS_WINDOWS)
// --- Windows Implementation ---
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
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return buf;
}
#else
// --- POSIX (dlfcn) Implementation ---
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
    if (sv_isobject(ST(0)) && sv_derived_from(ST(0), "Affix::Lib")) {
        lib = INT2PTR(DLLib, SvIV(SvRV(ST(0))));
    }
    else if (SvPOK(ST(0))) {
        lib = load_library(SvPV_nolen(ST(0)));
    }
    else {
        croak("First argument must be an Affix::Lib object or a library path");
    }
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
    else {
        croak("lib is not of type Affix::Lib");
    }
    XSRETURN_EMPTY;
}
// --- Affix Struct Management ---
void destroy_affix(pTHX_ Affix * affix) {
    if (affix == NULL)
        return;
    if (affix->trampoline)
        ffi_trampoline_free(affix->trampoline);
    if (affix->type_arena)
        arena_destroy(affix->type_arena);
    safefree(affix);
}
// --- The Runtime Trigger ---
extern void _Affix_trigger_infix(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    Affix * affix = (Affix *)XSANY.any_ptr;
    size_t items = (SP - MARK);

    if (UNLIKELY(items != affix->num_args))
        croak("Wrong number of arguments to affixed function: expected %" UVuf ", got %d", (UV)affix->num_args, items);

    ffi_cif_func jit_func = (ffi_cif_func)ffi_trampoline_get_code(affix->trampoline);
    if (UNLIKELY(!jit_func))
        croak("Internal Affix Error: JIT trampoline is not executable.");

    size_t total_arg_data_size = 0;
    for (size_t i = 0; i < affix->num_args; ++i)
        total_arg_data_size += (affix->arg_types[i]->size + 15) & ~15;

    char * c_args_buffer = (char *)alloca(total_arg_data_size);
    void ** args_array = (void **)alloca(affix->num_args * sizeof(void *));
    char * current_arg_ptr = c_args_buffer;

    for (int i = 0; i < items; ++i) {
        current_arg_ptr = (char *)(((uintptr_t)current_arg_ptr + 15) & ~15);
        marshal_sv_to_c(aTHX_ current_arg_ptr, ST(i), affix->arg_types[i]);
        args_array[i] = current_arg_ptr;
        current_arg_ptr += affix->arg_types[i]->size;
    }

    AFFIX_ALIGNAS(_Alignof(long double)) char return_buffer[affix->return_type->size];
    jit_func(affix->symbol, &return_buffer, args_array);
    if (affix->return_type->category == FFI_TYPE_VOID)
        PL_stack_sp = PL_stack_base + ax - 1;
    else {
        SV * return_sv = fetch_c_to_sv(aTHX_ & return_buffer, affix->return_type);
        ST(0) = return_sv;
        PL_stack_sp = PL_stack_base + ax;
    }
}
// --- Setup and Teardown ---
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
    Affix * affix;
    Newxz(affix, 1, Affix);
    affix->symbol = funcptr;

    ffi_status status = ffi_signature_parse(signature,
                                            &affix->type_arena,
                                            &affix->return_type,
                                            &affix->arg_types,
                                            &affix->num_args,
                                            &affix->num_fixed_args);
    if (status != FFI_SUCCESS) {
        safefree(affix);
        croak("Failed to parse FFI signature string: '%s' [%d != %d]", signature, status, FFI_SUCCESS);
    }
    status = generate_forward_trampoline(
        &affix->trampoline, affix->return_type, affix->arg_types, affix->num_args, affix->num_fixed_args);
    if (status != FFI_SUCCESS) {
        destroy_affix(aTHX_ affix);
        croak("Failed to generate FFI trampoline for signature: '%s' [%d]", signature, status);
    }

    char * prototype = (char *)safemalloc(affix->num_args + 1);
    memset(prototype, '$', affix->num_args);
    prototype[affix->num_args] = '\0';

    STMT_START {
        cv = newXSproto_portable(install_name, _Affix_trigger_infix, __FILE__, prototype);
        safefree(prototype);
        if (!cv) {
            destroy_affix(aTHX_ affix);
            croak("Failed to create new XSUB");
        }
        XSANY.any_ptr = (void *)affix;
    }
    STMT_END;

    SV * ret_sv = sv_bless(newRV_inc(MUTABLE_SV(cv)), gv_stashpv("Affix", GV_ADD));
    ST(0) = (ix == 1) ? newRV_inc(MUTABLE_SV(cv)) : ret_sv;
    XSRETURN(1);
}

XS_INTERNAL(Affix_DESTROY) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    Affix * affix;
    STMT_START {  // peel this grape
        HV * st;
        GV * gvp;
        SV * const xsub_tmp_sv = ST(0);
        SvGETMAGIC(xsub_tmp_sv);
        CV * cv = sv_2cv(xsub_tmp_sv, &st, &gvp, 0);
        affix = (Affix *)XSANY.any_ptr;
    }
    STMT_END;
    destroy_affix(aTHX_ affix);
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
