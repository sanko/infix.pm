/**
 * @file Affix.h
 * @brief Main internal header for the Affix Perl module.
 *
 * @details This header file defines all core internal data structures, macros,
 * and function prototypes used throughout Affix.
 */

#pragma once

#define PERL_NO_GET_CONTEXT 1 /* we want efficiency */
#include <EXTERN.h>
#include <perl.h>
#define NO_XSLOCKS /* for exceptions */
#include <XSUB.h>

#include "common/infix_config.h"
#include "infix/infix.h"  // The core 'infix' FFI library header

#include <inttypes.h>

// Perl version compat
#ifndef sv_setbool_mg
#define sv_setbool_mg(sv, b) sv_setsv_mg(sv, boolSV(b)) /* new in perl 5.36 */
#endif
#ifndef newSVbool
#define newSVbool(b) boolSV(b) /* new in perl 5.36 */
#endif
#ifndef sv_setbool
#define sv_setbool sv_setsv /* new in perl 5.38 */
#endif

// in CORE as of perl 5.40 but I might try to support older perls without ppp
// #if PERL_VERSION_LT(5, 40, 0)
#if PERL_VERSION_MINOR < 40
#define newAV_mortal() MUTABLE_AV(sv_2mortal((SV *)newAV()))
#endif
#define newHV_mortal() MUTABLE_HV(sv_2mortal((SV *)newHV()))

#define hv_existsor(hv, key, _or) hv_exists(hv, key, strlen(key)) ? *hv_fetch(hv, key, strlen(key), 0) : _or

#ifdef MULTIPLICITY
#define storeTHX(var) (var) = aTHX
#define dTHXfield(var) tTHX var;
#else
#define storeTHX(var) dNOOP
#define dTHXfield(var)
#endif

// --- Dynamic Library Loading Abstraction ---
#if defined(INFIX_OS_WINDOWS)
// --- Windows Implementation ---
#include <windows.h>
typedef HMODULE DLLib;
#else
// --- POSIX (dlfcn) Implementation ---
#include <dlfcn.h>
typedef void * DLLib;
#endif

DLLib load_library(const char * lib);
void * find_symbol(DLLib lib, const char * name);
void free_library(DLLib lib);
const char * get_dl_error();

// Portable alignment macro to replace C11 `alignas`
#if defined(__GNUC__) || defined(__clang__)
#define AFFIX_ALIGNAS(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define AFFIX_ALIGNAS(n) __declspec(align(n))
#else
#define AFFIX_ALIGNAS(n)
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

// Only in 5.38.0+
#ifndef PERL_ARGS_ASSERT_NEWSV_FALSE
#define newSV_false() newSVsv(&PL_sv_no)
#endif

#ifndef PERL_ARGS_ASSERT_NEWSV_TRUE
#define newSV_true() newSVsv(&PL_sv_yes)
#endif

#define dcAllocMem safemalloc
#define dcFreeMem safefree

#ifndef av_count
#define av_count(av) (AvFILL(av) + 1)
#endif


// Context key for thread-local storage in Perl
#define MY_CXT_KEY "Affix::_cxt" XS_VERSION

/**
@struct Affix
@brief Holds the state for a single affixed (JIT-compiled) C function.
@details This structure is the heart of an affixed function. It stores the
pointer to the target C function and all the necessary metadata generated
by the 'infix' library. This includes the handle to the executable trampoline
and the parsed type information, which is used as a blueprint for data
marshalling during calls.
*/
typedef infix_forward_t * Affix;

// --- Internal struct to hold trampoline and symbol ---
typedef struct {
    Affix trampoline;  // Affix is typedef'd to infix_forward_t*
    void * symbol;
} Affix_Context;


/**
 * @struct Affix_Pointer
 * @brief The internal C struct that backs every Affix::Pointer Perl object.
 * @details This is the core data structure for representing a C pointer.
 *          It holds not just the raw address, but also the metadata required
 *          for typed dereferencing, iteration, and memory management.
 */
typedef struct {
    /// @brief The base C memory address of the allocated block. This address does not change.
    void * address;
    /// @brief An infix `infix_type` that describes the data for a SINGLE ELEMENT
    /// that this pointer is pointing to. Used for offset calculations and dereferencing.
    infix_type * type;
    /// @brief The arena that owns the `type` graph. This must be destroyed
    /// when the Affix_Pointer object is destroyed.
    infix_arena_t * type_arena;
    /// @brief The total number of elements of `type` that fit in the memory block
    /// starting at `address`. Used for bounds checking.
    size_t count;
    /// @brief The current position for iterator-style access, in element counts.
    /// This value is modified by the ++ and -- operators.
    size_t position;
    /// @brief If true, Affix is responsible for `safefree()`ing the `address`
    /// when this object is destroyed.
    bool managed;
} Affix_Pointer;

/**
@struct Affix_Pin
@brief Manages the binding between a Perl scalar and a native C variable.
@details This struct is attached via Perl's magic to a scalar. It holds a
pointer to the native C data and an infix_type descriptor that tells the
marshalling code how to convert data between Perl's SV representation and the
C memory layout.
*/
typedef struct {
    /// @brief The raw pointer to the C data.
    void * pointer;
    /// @brief The infix_type that describes the C data, used for marshalling.
    infix_type * type;
    /// This is kept for proper memory management.
    infix_arena_t * type_arena;
    /// @brief If true, Affix is responsible for free()ing the pointer when the pin is destroyed.
    bool managed;
} Affix_Pin;

/**
 * @struct Affix_Callback_Data
 * @brief A context struct to hold the state for a single callback.
 * @details This struct is passed as the `user_data` to infix's reverse
 *          trampoline. It acts as a bridge, allowing our generic C handler to
 *          find the specific Perl subroutine and type information it needs to execute.
 */
typedef struct {
    /// @brief The Perl CV* to call
    SV * perl_sub;
    /// @brief The Perl interpreter context for thread safety
    dTHXfield(perl)
        /// @brief The return type of the callback.
        const infix_type * return_type;
    /// @brief An array of types for the callback's arguments.
    const infix_type ** arg_types;
    /// @brief The number of arguments.
    size_t num_args;
} Affix_Callback_Data;

// Affix.c - Core FFI logic & Library Loading
void destroy_affix(pTHX_ Affix_Context * context);
DLLib load_library(const char * lib);
void free_library(DLLib lib);
void * find_symbol(DLLib lib, const char * name);

// marshal.c - Data conversion between Perl SVs and C types
void marshal_sv_to_c(pTHX_ void * dest_c, SV * src_sv, const infix_type * type_info);
SV * fetch_c_to_sv(pTHX_ void * src_c, const infix_type * type_info);

// pin.c - Logic for tying SVs to C pointers
void pin(pTHX_ infix_arena_t *, infix_type *, SV *, void *, bool);
bool is_pin(pTHX_ SV *);
Affix_Pin * get_pin(pTHX_ SV *);

// Callback.c - Reverse trampoline system
void _Affix_callback_handler(infix_context_t * context, void * return_value_ptr, void ** args_array);

// utils.c - General helper functions
#define export_function(package, what, tag) \
    _export_function(aTHX_ get_hv(form("%s::EXPORT_TAGS", package), GV_ADD), what, tag)
void register_constant(const char * package, const char * name, SV * value);
void _export_function(pTHX_ HV * _export, const char * what, const char * _tag);
void export_constant_char(const char * package, const char * name, const char * _tag, char val);
void export_constant(const char * package, const char * name, const char * _tag, double val);
void set_isa(const char * klass, const char * parent);

// utils.c - Debugging helpers
#if DEBUG > 1
#define PING warn("Ping at %s line %d", FILE, LINE);
#else
#define PING
#endif

// The macro `DumpHex` calls the real function `_DumpHex`.
// Only the real function `_DumpHex` gets a prototype.
#define DumpHex(addr, len) _DumpHex(aTHX_ addr, len, __FILE__, __LINE__)
void _DumpHex(pTHX_ const void * addr, size_t len, const char * file, int line);

#define DD(scalar) _DD(aTHX_ scalar, __FILE__, __LINE__)
void _DD(pTHX_ SV * scalar, const char * file, int line);

// XS Bootstrapping Functions (one for each Perl package)
void boot_Affix_Platform(pTHX_ CV *);
void boot_Affix_Pointer(pTHX_ CV *);
void boot_Affix_pin(pTHX_ CV *);
void boot_Affix_Callback(pTHX_ CV *);
