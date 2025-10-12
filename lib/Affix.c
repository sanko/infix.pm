#include "Affix.h"

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
XS_INTERNAL(Affix_find_symbol) {}
XS_INTERNAL(Affix_dlerror) {}

//
void push_int32(pTHX_ const infix_type * type, SV * sv, void * ptr) {
    int x = SvIV(sv);
    if (ptr == nullptr)
        Newxz(ptr, 1, int *);
    //~ Copy(&i, ptr, 1, int);

    Copy(&x, ptr, 1, int);


    //~ Newxz(args[1], 1, int *);
    //~ Copy(&x, args[1], 1, int);

    //~ Newxz(ptr, 1, int *);
    //~ Copy(&x, ptr, 1, int);

    warn("in push_int32");
}

//
extern void Affix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;

    Affix * affix = (Affix *)XSANY.any_ptr;
    size_t items = (SP - MARK);

    dMY_CXT;
    // size_t *cvm_x = MY_CXT.x;

    infix_cif_func cif = (infix_cif_func)infix_forward_get_code(affix->infix);
    //


    /*

// User takes responsibility for packing the data.
infix_arena_t* arena = infix_arena_create(128);

// Allocate and fill the arguments contiguously.
int*    p_a = infix_arena_alloc(arena, sizeof(int),    _Alignof(int));    *p_a = 10;
double* p_b = infix_arena_alloc(arena, sizeof(double), _Alignof(double)); *p_b = 3.14;
char*   p_c = infix_arena_alloc(arena, sizeof(char),   _Alignof(char));   *p_c = 'X';

// Create the void** array, which is still required by the API.
void* args[] = { p_a, p_b, p_c }; // This array now points to a tight block of memory.

// Call the trampoline.
cif_func(target_fn, &ret_buf, args);

infix_arena_destroy(arena);

    */


    //~ c23_nodiscard infix_arena_t * infix_arena_create	(	size_t 	initial_size	)

    infix_arena_t * arena = infix_arena_create(sizeof(void *) * 2);


    //~ int my_int_1 = SvIV(ST(0));
    //~ int my_int_2 = SvIV(ST(1));
    int ret;

    //~ void * args[2] = {&my_int_1, &my_int_2};
    void ** args;
    Newxz(args, 2, void *);
    warn("Here 1!");

    Newxz(args[0], 1, int *);
    Newxz(args[1], 1, int *);

    for (Stack_off_t st = 0; st < infix_forward_get_num_args(affix->infix); st++)
        affix->push[st](aTHX_ infix_forward_get_arg_type(affix->infix, st), ST(st), args[st]);

    warn("Finished pushing params!");

    warn("Here1.1!");

    DumpHex(args, 16);
    //~ DumpHex(args[0], 16);
    //~ DumpHex(args[1], 16);

    //
    if (affix->symbol != nullptr)
        cif(&ret, args);
    warn("Here 2!");
    //~ XSRETURN_IV(printf_ret);

    PL_stack_sp = PL_stack_base + ax + (((ST(0) = newSViv(ret))) ? 0 : -1);
    return;
}

void Affix_destroy(pTHX_ infix_forward_t * ctx) {}

//~ typedef void (*push)(aTHX_ SV *, void *);

XS_INTERNAL(Affix_affix) {
    // ix == 0 if Affix::affix
    // ix == 1 if Affix::wrap
    dXSARGS;
    dXSI32;
    PERL_UNUSED_VAR(items);

    if (items != 3)
        croak("Usage: function($library, $symbol, $signature)");

    Affix * affix = nullptr;
    Newxz(affix, 1, Affix);

    SV * const xsub_tmp_sv = ST(0);
    SvGETMAGIC(xsub_tmp_sv);

    {
        infix_forward_t * infix = nullptr;

        Affix_Lib libhandle = nullptr;
        {
            if (!SvOK(xsub_tmp_sv) && SvREADONLY(xsub_tmp_sv))  // explicit undef
                libhandle = load_library(nullptr);
            else if (sv_isobject(xsub_tmp_sv) && sv_derived_from(xsub_tmp_sv, "Affix::Lib")) {
                IV tmp = SvIV((SV *)SvRV(xsub_tmp_sv));
                libhandle = INT2PTR(Affix_Lib, tmp);
            }
            else if (NULL == (libhandle = load_library(SvPV_nolen(xsub_tmp_sv)))) {
                Stat_t statbuf;
                Zero(&statbuf, 1, Stat_t);
                if (PerlLIO_stat(SvPV_nolen(xsub_tmp_sv), &statbuf) < 0) {
                    ENTER;
                    SAVETMPS;
                    PUSHMARK(SP);
                    XPUSHs(xsub_tmp_sv);
                    PUTBACK;
                    SSize_t count = call_pv("Affix::find_library", G_SCALAR);
                    SPAGAIN;
                    if (count == 1)
                        libhandle = load_library(SvPV_nolen(POPs));
                    PUTBACK;
                    FREETMPS;
                    LEAVE;
                }
            }
        }
        if (libhandle == nullptr)
            XSRETURN_UNDEF;  // User could check get_dlerror()
                             //
        const char * symbol = SvPVbyte_nolen(ST(1));
        affix->symbol = find_symbol(libhandle, symbol);
        if (affix->symbol == nullptr) {
            free_library(libhandle);
            XSRETURN_UNDEF;
        }

        //
        const char * signature = SvPVbyte_nolen(ST(2));
        infix_status status = infix_forward_create(&infix, signature, affix->symbol, NULL);
        if (status != INFIX_SUCCESS) {
            free_library(libhandle);
            //~ safefree(affix);
            XSRETURN_UNDEF;
        }
        affix->infix = infix;
    }

    //~ infix_cif_func cif = (infix_cif_func)infix_forward_get_code(affix->infix);
    //
    Newxz(affix->push, infix_forward_get_num_args(affix->infix), Affix_Push);
    for (size_t i = 0; i < infix_forward_get_num_args(affix->infix); ++i)
        affix->push[i] = push_int32;


    //~ c23_nodiscard size_t ;

    //
    char * prototype = "$$";
    char * rename = "add";

    STMT_START {
        cv = newXSproto_portable(ix == 0 ? rename : nullptr, Affix_trigger, __FILE__, prototype);
        if (UNLIKELY(cv == NULL))
            croak("ARG! Something went really wrong while installing a new XSUB!");
        XSANY.any_ptr = (void *)affix;
    }
    STMT_END;
    ST(0) = sv_2mortal(sv_bless((UNLIKELY(ix == 1) ? newRV_noinc(MUTABLE_SV(cv)) : newRV_inc(MUTABLE_SV(cv))),
                                gv_stashpv("Affix", GV_ADD)));
}
XS_INTERNAL(Affix_DESTROY) {}
XS_INTERNAL(Affix_END) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    dMY_CXT;  // XXX: If we need this
    XSRETURN_EMPTY;
}

// Pin system
static MGVTBL Affix_pin_vtbl = {
    Affix_get_pin,   // get
    Affix_set_pin,   // set
    nullptr,         // len
    nullptr,         // clear
    Affix_free_pin,  // free
    nullptr,         // copy
    nullptr,         // dup
    nullptr          // local
};

void delete_pin(pTHX_ Affix_Pin * pin) {
    if (pin == nullptr)
        return;

    // If this pin "owns" the C memory, free it.
    if (pin->managed && pin->pointer != nullptr)
        safefree(pin->pointer);

    // The infix_type is stored in an arena owned by the pin. Destroying the
    // arena frees the type graph.
    if (pin->type_arena != nullptr)
        infix_arena_destroy(pin->type_arena);
    safefree(pin);
}

int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg) {
    warn("Line: %d", __LINE__);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    warn("Line: %d", __LINE__);

    if (pin == nullptr || pin->type == nullptr || pin->pointer == nullptr) {
        warn("Line: %d", __LINE__);
        sv_setsv_mg(sv, &PL_sv_undef);
        warn("Line: %d", __LINE__);
        return 0;
    }
    warn("Line: %d", __LINE__);

    // Delegate to the centralized marshalling function.
    SV * val;
    ptr2sv(aTHX_ pin->pointer, val, pin->type);
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
    sv2ptr(aTHX_ pin->pointer, sv, pin->type);
    return 0;
}

int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    delete_pin(aTHX_ pin);
    mg->mg_ptr = NULL;  // Prevent double-free
    return 0;
}

void pin(pTHX_ infix_arena_t * type_arena, const infix_type * type, SV * sv, void * ptr, bool managed) {}

XS_INTERNAL(Affix_pin) {}
XS_INTERNAL(Affix_unpin) {}
XS_INTERNAL(Affix_is_pin) {}

// Callback system
void Affix_Callback_Handler(infix_context_t * ctx, void * retval, void ** args) {}

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
    ST(0) = sv_2mortal(_Affix_gen_dualvar(aTHX_ SvIV(ST(0)), SvPVbyte_nolen(ST(1))));
    XSRETURN(1);
}

SV * _Affix_gen_dualvar(pTHX_ IV iv, const char * pv) {
    SV * sv = newSVpvn_share(pv, strlen(pv), 0);
    (void)SvUPGRADE(sv, SVt_PVIV);
    SvIV_set(sv, iv);
    SvIandPOK_on(sv);
    return sv;
}

// Marshal
void ptr2sv(pTHX_ void *, SV *, const infix_type *) {}
void sv2ptr(pTHX_ SV *, void *, const infix_type *) {}

//
void register_constant(const char * package, const char * name, SV * value) {
    dTHX;
    HV * stash = gv_stashpv(package, GV_ADD);
    newCONSTSUB(stash, (char *)name, value);
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

void export_constant(const char * package, const char * name, const char * _tag, double val) {
    dTHX;
    register_constant(package, name, newSVnv(val));
    // Assumes an `export_function` macro exists in a header.
    // We will use the direct call for clarity.
    _export_function(aTHX_ get_hv(form("%s::EXPORT_TAGS", package), GV_ADD), name, _tag);
}
void set_isa(const char * package, const char * parent) {
    dTHX;
    // Ensure the parent package's stash exists
    gv_stashpv(parent, GV_ADD | GV_ADDMULTI);
    // Push parent onto @ISA
    av_push(get_av(form("%s::ISA", package), TRUE), newSVpv(parent, 0));
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

// Debugging
void _DumpHex(pTHX_ const void * addr, size_t len, const char * file, int line) {
    fflush(stdout);
    int perLine = 16;
    // Silently ignore silly per-line values.
    if (perLine < 4 || perLine > 64)
        perLine = 16;
    size_t i;
    U8 * buff;
    Newxz(buff, perLine + 1, U8);
    const U8 * pc = (const U8 *)addr;
    printf("Dumping %lu bytes from %p at %s line %d\n", len, addr, file, line);
    // Length checks.
    if (len == 0)
        croak("ZERO LENGTH");
    for (i = 0; i < len; i++) {
        if ((i % perLine) == 0) {  // Only print previous-line ASCII buffer for
            // lines beyond first.
            if (i != 0)
                printf(" | %s\n", buff);
            printf("#  %03zu ", i);  // Output the offset of current line.
        }
        // Now the hex code for the specific character.
        printf(" %02x", pc[i]);
        // And buffer a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))  // isprint() may be better.
            buff[i % perLine] = '.';
        else
            buff[i % perLine] = pc[i];
        buff[(i % perLine) + 1] = '\0';
    }
    // Pad out last line if not exactly perLine characters.
    while ((i % perLine) != 0) {
        printf("   ");
        i++;
    }
    printf(" | %s\n", buff);
    fflush(stdout);
}
//
void boot_Affix(pTHX_ CV * cv) {
    dXSBOOTARGSXSAPIVERCHK;
    PERL_UNUSED_VAR(items);
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
    (void)newXSproto_portable("Affix::affix", Affix_affix, __FILE__, "$$$");
    XSANY.any_i32 = 0;
    // Affix::wrap(  lib, symbol, [args], return )
    //             ( [lib, version], symbol, [args], return )
    (void)newXSproto_portable("Affix::wrap", Affix_affix, __FILE__, "$$$");
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

    //
    Perl_xs_boot_epilog(aTHX_ ax);
}
