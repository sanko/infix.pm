#include "Affix.h"

#define MY_CXT_KEY "Affix::_guts" XS_VERSION

typedef struct { size_t x; } my_cxt_t;

START_MY_CXT

#if defined(INFIX_OS_WINDOWS)
Affix_Lib load_library(const char * lib) {
    return LoadLibraryA(lib);
}
void free_library(Affix_Lib lib) {
    if (lib)
        FreeLibrary(lib);
}
void * find_symbol(Affix_Lib lib, const char * name) {
    return (void *)GetProcAddress(lib, name);
}
const char * get_dlerror() {
    static char buf[1024];
    DWORD dw = GetLastError();
    if (dw == 0)
        return NULL;
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, 0, (LPSTR)buf, sizeof(buf), NULL);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return buf;
}
#else
DLLib load_library(const char * lib) {
    return dlopen(lib, RTLD_LAZY | RTLD_GLOBAL);
}
void free_library(DLLib lib) {
    if (lib)
        dlclose(lib);
}
void * find_symbol(DLLib lib, const char * name) {
    return dlsym(lib, name);
}
const char * get_dlerror() {
    return dlerror();
}
#endif

XS_INTERNAL(Affix_load_library) {}
XS_INTERNAL(Affix_free_library) {}
XS_INTERNAL(Affix_find_symbol)  {}
XS_INTERNAL(Affix_dlerror) {}

//
extern void Affix_trigger(pTHX_ CV * cv){
    dSP;
    dAXMARK;

    Affix *affix = (Affix *)XSANY.any_ptr;
    size_t items = (SP - MARK);

    dMY_CXT;
// size_t *cvm_x = MY_CXT.x;

    }
void Affix_destroy(pTHX_ infix_forward * context){}
XS_INTERNAL(Affix_affix) {}
XS_INTERNAL(Affix_DESTROY) {}
XS_INTERNAL(Affix_END) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    dMY_CXT;

    XSRETURN_EMPTY;
}

// Pin system
void delete_pin(pTHX_ Affix_Pin * pin) {
    if (pin == NULL)
        return;

    // If this pin "owns" the C memory, free it.
    if (pin->managed && pin->pointer != NULL)
        safefree(pin->pointer);

    // The infix_type is stored in an arena owned by the pin. Destroying the
    // arena frees the type graph.
    if (pin->type_arena != NULL)
        infix_arena_destroy(pin->type_arena);
    safefree(pin);
}

int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg) {
    warn("Line: %d", __LINE__);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    warn("Line: %d", __LINE__);

    if (pin == NULL || pin->type == NULL || pin->pointer == NULL) {
        warn("Line: %d", __LINE__);
        sv_setsv_mg(sv, &PL_sv_undef);
        warn("Line: %d", __LINE__);
        return 0;
    }
    warn("Line: %d", __LINE__);

    // Delegate to the centralized marshalling function.
    SV * val = ptr2sv(aTHX_ pin->pointer, pin->type);
    sv_setsv_mg(sv, val);
    warn("Line: %d, %p", __LINE__, pin->pointer);

    return 0;
}

int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (pin == NULL || pin->type == NULL || pin->pointer == NULL) {
        // Writing to an invalid pin is a no-op that results in undef.
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }
    // Delegate to the centralized marshalling function.
    marshal_sv_to_c(aTHX_ pin->pointer, sv, pin->type);
    return 0;
}

int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    delete_pin(aTHX_ pin);
    mg->mg_ptr = NULL;  // Prevent double-free
    return 0;
}

static MGVTBL Affix_pin_vtbl = {
    Affix_get_pin,   // get
    Affix_set_pin,   // set
    NULL,            // len
    NULL,            // clear
    Affix_free_pin,  // free
    NULL,            // copy
    NULL,            // dup
    NULL             // local
};

void pin(pTHX_ infix_arena_t * type_arena, const infix_type * type, SV * sv, void * ptr, bool managed) {}

XS_INTERNAL(Affix_pin) {}
XS_INTERNAL(Affix_unpin) {}
XS_INTERNAL(Affix_is_pin) {}

// Utils
XS_INTERNAL(Affix_sv_dump) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$sv");
    sv_dump(ST(0));
    XSRETURN_EMPTY;
}

XS_INTERNAL(Affix_gen_dualvar) {
    Stack_off_t ax = 0;
    // dAXMARK;  // ST(...)
    ST(0) = sv_2mortal(gen_dualvar(aTHX_ SvIV(ST(0)), SvPVbyte_nolen(ST(1))));
    XSRETURN(1);
}

// Cribbed from Perl::Destruct::Level so leak testing works without yet another prereq
XS_INTERNAL(Affix_set_destruct_level) {
    dXSARGS;
    // TODO: report this with a warn(...)
    if (items != 1)
        croak_xs_usage(cv, "level");
    PL_perl_destruct_level = SvIV(ST(0));
    XSRETURN_EMPTY;
}

//
void boot_Affix(pTHX_ CV * cv){
    dXSBOOTARGSXSAPIVERCHK;
    PERL_UNUSED_VAR(items);
    // PERL_UNUSED_VAR(items);
#ifdef USE_ITHREADS  // Windows...
    my_perl = (PerlInterpreter *)PERL_GET_CONTEXT;
#endif
    MY_CXT_INIT;
    // Allow user defined value in a BEGIN{ } block
    SV * vmsize = get_sv("Affix::VMSize", 0);
    //

    // Affix::affix( lib, symbol, [args], return )
    //             ( [lib, version], symbol, [args], return )
    //             ( lib, [symbol, name], [args], return )
    //             ( [lib, version], [symbol, name], [args], return )
    (void)newXSproto_portable("Affix::affix", Affix_affix, __FILE__, "$$$;$");
    XSANY.any_i32 = 0;
    // Affix::wrap(  lib, symbol, [args], return )
    //             ( [lib, version], symbol, [args], return )
    (void)newXSproto_portable("Affix::wrap", Affix_affix, __FILE__, "$$$$");
    XSANY.any_i32 = 1;
    (void)newXSproto_portable("Affix::DESTROY", Affix_DESTROY, __FILE__, "$");
    (void)newXSproto_portable("Affix::END", Affix_END, __FILE__, "");

    (void)newXSproto_portable("Affix::load_library", Affix_load_library, __FILE__, "$");
    (void)newXSproto_portable("Affix::free_library", Affix_free_library, __FILE__, "$");
    (void)newXSproto_portable("Affix::find_symbol", Affix_find_symbol, __FILE__, "$$");
    (void)newXSproto_portable("Affix::dlerror", Affix_dlerror, __FILE__, "");

    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");

    //
    (void)newXSproto_portable("Affix::pin", Affix_pin, __FILE__, "$$$$");
    export_function("Affix", "pin", "base");
    (void)newXSproto_portable("Affix::unpin", Affix_unpin, __FILE__, "$");
    export_function("Affix", "unpin", "base");
    (void)newXSproto_portable("Affix::is_pin", Affix_is_pin, __FILE__, "$");


    (void)newXSproto_portable("Affix::set_destruct_level", Affix_set_destruct_level, __FILE__, "$");
    (void)newXSproto_portable("Affix::sv_dump", Affix_sv_dump, __FILE__, "$");
    (void)newXSproto_portable("Affix::gen_dualvar", Affix_gen_dualvar, __FILE__, "$$");

    (void)newXSproto_portable("Affix::load_library", Affix_load_library, __FILE__, "$");
    (void)newXSproto_portable("Affix::free_library", Affix_free_library, __FILE__, "$;$");
    (void)newXSproto_portable("Affix::list_symbols", Affix_list_symbols, __FILE__, "$");
    (void)newXSproto_portable("Affix::find_symbol", Affix_find_symbol, __FILE__, "$$");
    (void)newXSproto_portable("Affix::dlerror", Affix_dlerror, __FILE__, "");

    //
    Perl_xs_boot_epilog(aTHX_ ax);
}
