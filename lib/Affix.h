#pragma once

// Disables the implicit 'pTHX_' context pointer argument, which is good practice for
// modern Perl XS code that uses the 'aTHX_' macro explicitly.
#define PERL_NO_GET_CONTEXT 1
#include <EXTERN.h>
#include <perl.h>
// Disables Perl's internal locking mechanisms for certain structures.
// This is often used when the XS module manages its own thread safety.
#define NO_XSLOCKS
#include <XSUB.h>

// Redirect infix's internal memory allocation to use Perl's safe allocation functions.
// This ensures all memory is tracked by Perl's memory manager, which is safer and
// helps with leak detection tools like valgrind.
#define infix_malloc safemalloc
#define infix_calloc safecalloc
#define infix_free safefree
#define infix_realloc saferealloc

// This include order is critical. We need Perl's types defined first.
#include "common/infix_internals.h"
#include <infix/infix.h>

// This structure defines the thread-local storage for our module. Under ithreads,
// each Perl thread will get its own private instance of this struct.
typedef struct {
    /// @brief A per-thread hash table to store loaded libraries.
    /// Maps library path -> LibRegistryEntry*.
    /// This prevents reloading the same .so/.dll and manages its lifecycle.
    HV * lib_registry;
    // NEW: A per-thread hash table to cache callback trampolines, preventing re-creation and leaks.
    // Maps the memory address of a Perl CV* to its corresponding Implicit_Callback_Magic* struct.
    HV * callback_registry;
    /// @brief Type alias for an infix type registry. Represents a collection of named types.
    infix_registry_t * registry;
} my_cxt_t;

START_MY_CXT;

// Helper macro to fetch a value from a hash if it exists, otherwise return a default.
#define hv_existsor(hv, key, _or) hv_exists(hv, key, strlen(key)) ? *hv_fetch(hv, key, strlen(key), 0) : _or

// Macros to handle passing the Perl interpreter context ('THX') explicitly,
// which is necessary for thread-safe code.
#ifdef MULTIPLICITY
#define storeTHX(var) (var) = aTHX
#define dTHXfield(var) tTHX var;
#else
#define storeTHX(var) dNOOP
#define dTHXfield(var)
#endif

/// @brief Function pointer type for a "push" operation: marshalling from Perl (SV) to C (void*).
typedef void (*Affix_Push)(pTHX_ const infix_type *, SV *, void *);
/// @brief Function pointer type for a "pull" operation: marshalling from C (void*) to Perl (SV).
typedef SV * (*Affix_Pull)(pTHX_ const infix_type *, void *);

// New struct for the pre-computed marshalling plan for out-parameters
typedef struct {
    size_t index;                     // The argument index
    const infix_type * pointee_type;  // The type of data to write back (e.g., 'int' for an '*int')
} OutParamInfo;


/// @brief Represents a forward FFI call (a Perl sub that calls a C function).
/// This struct is the "context" attached to the generated XS subroutine.
typedef struct {
    infix_forward_t * infix;       ///< Handle to the infix trampoline and type info.
    infix_arena_t * args_arena;    ///< Fast memory allocator for arguments during a call.
    infix_arena_t * ret_arena;     ///< Fast memory allocator for return value during a call.
    infix_cif_func cif;            ///< A direct function pointer to the JIT-compiled trampoline code.
    Affix_Push * push;             ///< An array of marshalling functions for the arguments (Perl -> C).
    Affix_Pull pull;               ///< A marshalling function for the return value (C -> Perl).
    infix_library_t * lib_handle;  ///< If affix() loaded a library itself, stores the handle for cleanup.
    // Marshalling plan for out-parameters
    OutParamInfo * out_param_info;
    size_t num_out_params;
} Affix;

/// @brief Represents an Affix::Pin object, a blessed Perl scalar that wraps a raw C pointer.
typedef struct {
    void * pointer;              ///< The raw C memory address.
    const infix_type * type;     ///< Infix's description of the data type at 'pointer'. Used for dereferencing.
    infix_arena_t * type_arena;  ///< Memory arena that owns the 'type' structure.
    bool managed;                ///< If true, Perl owns the 'pointer' and will safefree() it on DESTROY.
    UV ref_count;                ///< Refcount to prevent premature freeing when SVs are copied.
    size_t size;                 ///< Size of malloc'd void pointers.
} Affix_Pin;

/// @brief Holds the necessary data for a callback, specifically the Perl subroutine to call.
typedef struct {
    SV * coderef_rv;  ///< A reference (RV) to the Perl coderef. We hold this to keep it alive.
    dTHXfield(perl)   ///< The thread context in which the callback was created.
} Affix_Callback_Data;

/// @brief Internal struct holding the C resources that are magically attached
///        to a user's coderef (CV*) when it is first used as a callback.
typedef struct {
    infix_reverse_t * reverse_ctx;  ///< Handle to the infix reverse-call trampoline.
} Implicit_Callback_Magic;

/// @brief An entry in the thread-local library registry hash.
typedef struct {
    infix_library_t * lib;  ///< The handle to the opened library.
    UV ref_count;           ///< Reference count. The library is closed only when this reaches 0.
} LibRegistryEntry;

SV * pull_struct(pTHX_ const infix_type * type, void * p);
SV * pull_union(pTHX_ const infix_type * type, void * p);
SV * pull_array(pTHX_ const infix_type * type, void * p);
void push_reverse_trampoline(pTHX_ const infix_type * type, SV * sv, void * p);
SV * pull_reverse_trampoline(pTHX_ const infix_type * type, void * p);
SV * pull_enum(pTHX_ const infix_type * type, void * p);
SV * pull_complex(pTHX_ const infix_type * type, void * p);
SV * pull_vector(pTHX_ const infix_type * type, void * p);

// The C function that gets executed when an affixed Perl sub is called.
extern void Affix_trigger(pTHX_ CV *);
// Marshals a C value from a pointer into an existing Perl SV.
void ptr2sv(pTHX_ void * c_ptr, SV * perl_sv, const infix_type * type);
// Marshals a Perl SV's value into a C pointer.
void sv2ptr(pTHX_ SV * perl_sv, void * c_ptr, const infix_type * type);
// Marshals a Perl HASH ref into a C struct.
void push_struct(pTHX_ const infix_type * type, SV * sv, void * p);
// Attaches a pin to a raw SV, making it magical.
void _pin_sv(pTHX_ SV * sv, const infix_type * type, void * pointer, bool managed);

// Functions implementing the "magic" for Affix::Pin objects (for dereferencing).
int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg);
int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg);
U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg);
int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg);
bool is_pin(pTHX_ SV * sv);

// The C function that gets called when a magically-promoted CV* is destroyed.
// NEW: This function is now OBSOLETE and can be removed.
// int Affix_free_implicit_callback(pTHX_ SV * sv, MAGIC * mg);

// Internal helper to safely get the Affix_Pin struct from a blessed SV.
Affix_Pin * _get_pin_from_sv(pTHX_ SV * sv);
// The C entry point for all reverse-FFI callbacks.
void _affix_callback_handler_entry(infix_context_t *, void *, void **);

// A portable way to create a new XS subroutine with a C prototype.
#ifdef newXS_flags
#define newXSproto_portable(name, c_impl, file, proto) newXS_flags(name, c_impl, file, proto, 0)
#else
#define newXSproto_portable(name, c_impl, file, proto) \
    (PL_Sv = (SV *)newXS(name, c_impl, file), sv_setpv(PL_Sv, proto), (CV *)PL_Sv)
#endif
// A macro to call Perl's newXS_deffile with the interpreter context.
#define newXS_deffile(a, b) Perl_newXS_deffile(aTHX_ a, b)
// Helper to add a function name to a package's export tags (e.g., %EXPORT_TAGS).
#define export_function(package, what, tag) \
    _export_function(aTHX_ get_hv(form("%s::EXPORT_TAGS", package), GV_ADD), what, tag)
void _export_function(pTHX_ HV *, const char *, const char *);

// The main entry point for the entire XS module, called by Perl on `use Affix`.
void boot_Affix(pTHX_ CV *);

// Debugging Macros
#define DEBUG 0

#if DEBUG > 1
// Simple macro to print the file and line number to stderr.
#define PING warn("Ping at %s line %d", __FILE__, __LINE__);
#else
#define PING
#endif

// A macro to print a hex dump of a memory region.
#define DumpHex(addr, len) _DumpHex(aTHX_ addr, len, __FILE__, __LINE__)
void _DumpHex(pTHX_ const void *, size_t, const char *, int);

// A macro to dump the internal structure of a Perl SV using Devel::Peek's logic.
#define DD(scalar) _DD(aTHX_ scalar, __FILE__, __LINE__)
void _DD(pTHX_ SV *, const char *, int);
