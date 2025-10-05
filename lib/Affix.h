#pragma once

#define PERL_NO_GET_CONTEXT 1 /* we want efficiency */
#include <EXTERN.h>
#include <perl.h>
#define NO_XSLOCKS /* for exceptions */
#include <XSUB.h>

// Use perl's memory allocation functions
#define infix_malloc safemalloc
#define infix_free safefree

#include "common/infix_config.h"
#include "infix/infix.h"  // The core 'infix' FFI library header

#define hv_existsor(hv, key, _or) hv_exists(hv, key, strlen(key)) ? *hv_fetch(hv, key, strlen(key), 0) : _or

#ifdef MULTIPLICITY
#define storeTHX(var) (var) = aTHX
#define dTHXfield(var) tTHX var;
#else
#define storeTHX(var) dNOOP
#define dTHXfield(var)
#endif

#if defined(INFIX_OS_WINDOWS)
#include <windows.h>
typedef HMODULE Affix_Lib;
#else
#include <dlfcn.h>
typedef void * Affix_Lib;
#endif

/* NOTE: the prototype of newXSproto() is different in versions of perls,
 * so we define a portable version of newXSproto()
 */
#ifdef newXS_flags
#define newXSproto_portable(name, c_impl, file, proto) newXS_flags(name, c_impl, file, proto, 0)
#else
#define newXSproto_portable(name, c_impl, file, proto) \
    (PL_Sv = (SV *)newXS(name, c_impl, file), sv_setpv(PL_Sv, proto), (CV *)PL_Sv)
#endif /* !defined(newXS_flags) */

#define newXS_deffile(a, b) Perl_newXS_deffile(aTHX_ a, b)

// Context key for thread-local storage in Perl
#define MY_CXT_KEY "Affix::_cxt" XS_VERSION

typedef struct {
    const void * address;
    const infix_type * type;
    infix_arena_t * type_arena;
    size_t position;
    bool managed;
} Affix_Pointer;

typedef struct {
    const void * pointer;
    const infix_type * type;
    infix_arena_t * type_arena;
    bool managed;
} Affix_Pin;

typedef struct {
    SV * perl_sub;
    dTHXfield(perl)
} Affix_Callback_Data;

//
void destroy_affix(pTHX_ infix_forward *);
Affix_Lib load_library(const char *);
void free_library(Affix_Lib);
void * find_symbol(Affix_Lib, const char *);
const char * get_dlerror();

//
void ptr2sv(pTHX_ void *, SV *, const infix_type *);
void sv2ptr(pTHX_ SV *, void *, const infix_type *);

//
void delete_pin(pTHX_ Affix_Pin * pin)
int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg);
int Affix_set_pin(pTHX_ SV *, MAGIC *);
int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg);
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
void pin(pTHX_ infix_arena_t *, const infix_type *, SV *, void *, bool);
bool is_pin(pTHX_ SV *);
Affix_Pin * get_pin(pTHX_ SV *);

//
void Affix_Callback_Handler(infix_config *, void *, void **);

//
#define export_function(package, what, tag) \
    _export_function(aTHX_ get_hv(form("%s::EXPORT_TAGS", package), GV_ADD), what, tag)
void register_constant(const char * , const char * , SV * );
void _export_function(pTHX_ HV * , const char * , const char * );
void export_constant_char(const char * , const char * , const char * , char );
void export_constant(const char * , const char * , const char * , double );
void set_isa(const char * , const char * );

//
SV * wchar2utf(pTHX_ wchar_t * , size_t );
wchar_t * utf2wchar(pTHX_ SV * , size_t );

//
#if DEBUG > 1
#define PING warn("Ping at %s line %d", FILE, LINE);
#else
#define PING
#endif

// The macro `DumpHex` calls the real function `_DumpHex`.
// Only the real function `_DumpHex` gets a prototype.
#define DumpHex(addr, len) _DumpHex(aTHX_ addr, len, __FILE__, __LINE__)
void _DumpHex(pTHX_ const void * , size_t , const char * , int );

#define DD(scalar) _DD(aTHX_ scalar, __FILE__, __LINE__)
void _DD(pTHX_ SV * , const char * , int );

// XS Bootstrapping Functions (one for each Perl package)
void boot_Affix_Platform(pTHX_ CV *);
void boot_Affix_Pointer(pTHX_ CV *);
void boot_Affix_pin(pTHX_ CV *);
void boot_Affix_Callback(pTHX_ CV *);
