/**
 * @file pin.c
 * @brief Implements the logic for pinning Perl variables to native C data.
 *
 * @details This file provides the "magic" that links a Perl scalar to a C memory
 * address. When the Perl scalar is read from (get), the C data is read,
 * converted, and returned. When the scalar is written to (set), the Perl value
 * is converted and written to the C memory address.
 *
 * This implementation has been refactored to use the `infix` FFI library's type
 * system. Instead of a complex, custom type struct, an `Affix_Pin` now holds an
 * `infix_type*` which serves as a complete blueprint for marshalling the data.
 */

#include "../Affix.h"

// --- Pin Lifecycle Management ---

/**
 * @brief Destroys an Affix_Pin struct and frees its associated resources.
 * @param pin The pin to destroy.
 */
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

// --- Perl Magic VTable Implementation ---

/**
 * @brief The magic 'get' handler. Called when a pinned Perl scalar is read.
 * @details It retrieves the `Affix_Pin` context, reads the C data from the
 *          pin's pointer, and uses the centralized `fetch_c_to_sv` marshaller
 *          to convert that data into a new Perl SV.
 * @return 0 on success.
 */
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
    SV * val = fetch_c_to_sv(aTHX_ pin->pointer, pin->type);
    sv_setsv_mg(sv, val);
    warn("Line: %d, %p", __LINE__, pin->pointer);

    return 0;
}

/**
 * @brief The magic 'set' handler. Called when a pinned Perl scalar is assigned to.
 * @details It retrieves the `Affix_Pin` context and uses the centralized
 *          `marshal_sv_to_c` function to convert the new Perl SV's value into C
 *          data, writing it to the pin's pointer address.
 * @return 0 on success.
 */
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

/**
 * @brief The magic 'free' handler. Called when the pinned SV is destroyed.
 * @details This function is the primary cleanup mechanism for a pin. It calls
 *          `delete_pin` to free the `Affix_Pin` struct and all its managed
 *          resources (the C pointer if managed, and the type arena).
 * @return 0 on success.
 */
int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    delete_pin(aTHX_ pin);
    mg->mg_ptr = NULL;  // Prevent double-free
    return 0;
}

// The virtual table that connects our C functions to Perl's magic system.
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


// --- Public C API for Pinning ---

/**
 * @brief Checks if a given SV is a pinned variable.
 * @param sv The Perl scalar to check.
 * @return true if the SV has Affix pin magic, false otherwise.
 */
bool is_pin(pTHX_ SV * sv) {
    return (sv && SvTYPE(sv) >= SVt_PVMG && mg_findext(sv, PERL_MAGIC_ext, &Affix_pin_vtbl)) ? true : false;
}

/**
 * @brief Retrieves the raw Affix_Pin struct from a pinned SV.
 * @param sv The pinned Perl scalar.
 * @return A pointer to the Affix_Pin struct, or NULL if the SV is not a pin.
 */
Affix_Pin * get_pin(pTHX_ SV * sv) {
    MAGIC * mg = mg_findext(sv, PERL_MAGIC_ext, &Affix_pin_vtbl);
    if (mg == NULL)
        return NULL;
    return (Affix_Pin *)mg->mg_ptr;
}

/**
 * @brief Attaches magic to an SV to pin it to a C memory location.
 * @param type The infix_type describing the C data.
 * @param sv The Perl scalar to attach the magic to.
 * @param ptr The C memory address to pin to.
 * @param managed If true, the C pointer will be safefree'd when the pin is destroyed.
 * @note This function takes ownership of the arena associated with the infix_type.
 */
void pin(pTHX_ infix_arena_t * type_arena, infix_type * type, SV * sv, void * ptr, bool managed) {
    warn("Line: %d", __LINE__);

    MAGIC * mg;

    if (is_pin(aTHX_ sv))
        mg = mg_findext(sv, PERL_MAGIC_ext, &Affix_pin_vtbl);
    else
        mg = sv_magicext(sv, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, NULL, 0);

    warn("Line: %d", __LINE__);

    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);
    pin->pointer = ptr;
    pin->managed = managed;
    pin->type_arena = type_arena;  // The pin now owns the arena.
    pin->type = type;
    warn("Line: %d", __LINE__);

    mg->mg_ptr = (char *)pin;
    mg_magical(sv);
    warn("Line: %d", __LINE__);
    warn("Line: %d, %p", __LINE__, ptr);
}

// --- XSUBs ---

/**
 * @brief XSUB for `Affix::pin`. Binds a Perl variable to an exported C global variable.
 *
 * @details Perl usage: `pin( $scalar, $lib_path, $symbol_name, $signature_string )`
 *
 * This function performs the following steps:
 * 1. Loads the specified dynamic library.
 * 2. Finds the address of the global variable (symbol) within that library.
 * 3. Uses the `infix` signature parser to create an `infix_type` object graph from
 *    the signature string. This creates an arena to hold the type.
 * 4. Calls the internal `pin()` function to attach magic to the Perl scalar,
 *    linking it to the symbol's address and storing the type information.
 */
XS_INTERNAL(Affix_pin) {
    dXSARGS;
    warn("Line: %d", __LINE__);
    if (items != 4)
        croak_xs_usage(cv, "$var, $lib, $symbol, $signature");
    warn("Line: %d", __LINE__);

    // 1. Load library and find the symbol's address
    DLLib lib = load_library(SvPV_nolen(ST(1)));
    if (!lib)
        croak("Failed to load library '%s'", SvPV_nolen(ST(1)));
    warn("Line: %d", __LINE__);

    char * symbol = SvPVbyte_nolen(ST(2));
    void * ptr = find_symbol(lib, symbol);
    if (ptr == NULL)
        croak("Failed to find symbol '%s' in %s", symbol, SvPV_nolen(ST(1)));
    warn("Line: %d", __LINE__);

    // 2. Create the infix_type from the signature string
    const char * signature = SvPV_nolen(ST(3));
    infix_arena_t * type_arena = NULL;
    infix_type * type = NULL;
    warn("Line: %d", __LINE__);

    infix_status status = infix_type_from_signature(&type, &type_arena, signature);
    warn("Line: %d", __LINE__);

    if (status != INFIX_SUCCESS || type == NULL) {
        if (type_arena)
            infix_arena_destroy(type_arena);
        croak("Failed to parse pin signature: '%s'", signature);
    }
    warn("Line: %d", __LINE__);

    // 3. Pin the variable. The `pin` function takes ownership of the arena.
    pin(aTHX_ type_arena, type, ST(0), ptr, false);  // `false` because we don't own the library's global variable
    warn("Line: %d", __LINE__);

    XSRETURN_YES;
}

/**
 * @brief XSUB for `Affix::unpin`. Removes the magic from a pinned variable.
 */
XS_INTERNAL(Affix_unpin) {
    dXSARGS;
    if (items != 1) {
        croak_xs_usage(cv, "$var");
    }
    if (sv_unmagicext(ST(0), PERL_MAGIC_ext, &Affix_pin_vtbl)) {
        XSRETURN_YES;
    }
    XSRETURN_NO;
}

/**
 * @brief XSUB for `Affix::is_pin`. Checks if a variable is pinned.
 */
XS_INTERNAL(Affix_is_pin) {
    dXSARGS;
    if (items != 1) {
        croak_xs_usage(cv, "$var");
    }
    if (is_pin(aTHX_ ST(0))) {
        XSRETURN_YES;
    }
    XSRETURN_NO;
}

void boot_Affix_pin(pTHX_ CV * cv) {
    PERL_UNUSED_VAR(cv);
    (void)newXSproto_portable("Affix::pin", Affix_pin, __FILE__, "$$$$");
    export_function("Affix", "pin", "base");
    (void)newXSproto_portable("Affix::unpin", Affix_unpin, __FILE__, "$");
    export_function("Affix", "unpin", "base");
    (void)newXSproto_portable("Affix::is_pin", Affix_is_pin, __FILE__, "$");
}
