#include "Affix.h"
#include <string.h>

// Use the modern `mg_virtual` field name for magic, as we target Perl 5.40+.
#define mg_vtbl mg_virtual

// ==============================================================================
// 1. Library Lifecycle Management (Thread-Safe with MY_CXT)
// ==============================================================================

XS_INTERNAL(Affix_Lib_as_string) {
    dVAR;
    dXSARGS;
    PING;
    if (items < 1)
        croak_xs_usage(cv, "$lib");
    IV RETVAL;
    {
        Affix_Lib lib;
        PING;
        //~ if (sv_derived_from(ST(0), "Affix:Lib")) {
        PING;
        IV tmp = SvIV((SV *)SvRV(ST(0)));
        lib = INT2PTR(Affix_Lib, tmp);
        PING;
        //~ }
        //~ else
        //~ croak("lib is not of type Affix::Lib");
        PING;
        RETVAL = PTR2IV(lib->handle);
    }
    PING;
    XSRETURN_IV(RETVAL);
};

XS_INTERNAL(Affix_Lib_DESTROY) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "lib_obj");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    Affix_Lib lib = INT2PTR(Affix_Lib, tmp);
    if (MY_CXT.lib_registry) {
        hv_iterinit(MY_CXT.lib_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.lib_registry))) {
            SV * entry_sv = HeVAL(he);
            LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
            if (entry->lib == lib) {
                entry->ref_count--;
                if (entry->ref_count == 0) {
                    infix_library_close(entry->lib);
                    Safefree(entry);
                    hv_delete_ent(MY_CXT.lib_registry, HeKEY_sv(he), G_DISCARD, 0);
                }
                break;
            }
        }
    }
    XSRETURN_EMPTY;
}

XS_INTERNAL(Affix_load_library) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "library_path");
    const char * path = SvPV_nolen(ST(0));
    SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, path, strlen(path), 0);
    if (entry_sv_ptr) {
        LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
        entry->ref_count++;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(entry->lib));
        ST(0) = sv_2mortal(sv_bless(newRV_inc(obj_data), gv_stashpv("Affix::Lib", GV_ADD)));
        XSRETURN(1);
    }
    Affix_Lib lib = infix_library_open(path);
    if (lib) {
        LibRegistryEntry * new_entry;
        Newxz(new_entry, 1, LibRegistryEntry);
        new_entry->lib = lib;
        new_entry->ref_count = 1;
        hv_store(MY_CXT.lib_registry, path, strlen(path), newSViv(PTR2IV(new_entry)), 0);
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(lib));
        ST(0) = sv_2mortal(sv_bless(newRV_inc(obj_data), gv_stashpv("Affix::Lib", GV_ADD)));
        XSRETURN(1);
    }
    XSRETURN_UNDEF;
}

XS_INTERNAL(Affix_get_last_error_message) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    infix_error_details_t err = infix_get_last_error();
    if (err.message[0] != '\0') {
        ST(0) = sv_2mortal(newSVpv(err.message, 0));
    }
#if defined(_WIN32)
    else if (err.system_error_code != 0) {
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL,
                       err.system_error_code,
                       0,
                       buf,
                       sizeof(buf),
                       NULL);
        ST(0) = sv_2mortal(newSVpvf("System error: %s (code %ld)", buf, err.system_error_code));
    }
#endif
    else {
        ST(0) = sv_2mortal(newSVpvf("Infix error code %d at position %zu", (int)err.code, err.position));
    }
    XSRETURN(1);
}

// ==============================================================================
// 2. Pin System (for DATA pointers)
// ==============================================================================

static MGVTBL Affix_pin_vtbl = {Affix_get_pin, Affix_set_pin, NULL, NULL, Affix_free_pin, NULL, NULL, NULL};

Affix_Pin * _get_pin_from_sv(pTHX_ SV * sv) {
    if (!sv_isobject(sv) || !sv_derived_from(sv, "Affix::Pin"))
        return NULL;
    MAGIC * mg = mg_find(SvRV(sv), PERL_MAGIC_ext);
    if (!mg || mg->mg_vtbl != &Affix_pin_vtbl)
        return NULL;
    return (Affix_Pin *)mg->mg_ptr;
}

int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (pin == NULL)
        return 0;
    if (pin->managed && pin->pointer)
        safefree(pin->pointer);
    if (pin->type_arena)
        infix_arena_destroy(pin->type_arena);
    Safefree(pin);
    mg->mg_ptr = NULL;
    return 0;
}
int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type) {
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }
    ptr2sv(aTHX_ pin->pointer, sv, pin->type);
    return 0;
}
int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg) {
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type)
        return 0;
    sv2ptr(aTHX_ sv, pin->pointer, pin->type);
    return 0;
}

U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg) {
    warn("len_pin");
    return 0;
}

static MGVTBL pin_vtbl_scalar = {
    Affix_get_pin,   // get
    Affix_set_pin,   // set
    Affix_len_pin,   // len
    NULL,            // clear
    Affix_free_pin,  // free
    NULL,            // copy
    NULL,            // dup
    NULL             // local
};

XS_INTERNAL(Affix_find_symbol) {
    dXSARGS;
    if (items != 2 || !sv_isobject(ST(0)) || !sv_derived_from(ST(0), "Affix::Lib"))
        croak_xs_usage(cv, "Affix_Lib_object, symbol_name");

    IV tmp = SvIV((SV *)SvRV(ST(0)));
    Affix_Lib lib = INT2PTR(Affix_Lib, tmp);
    const char * name = SvPV_nolen(ST(1));
    void * symbol = infix_library_get_symbol(lib, name);

    if (symbol) {
        Affix_Pin * pin;
        Newxz(pin, 1, Affix_Pin);
        pin->pointer = symbol;
        pin->managed = false;
        pin->type_arena = infix_arena_create(256);
        infix_type * void_ptr_type = NULL;
        if (infix_type_create_pointer_to(pin->type_arena, &void_ptr_type, infix_type_create_void()) != INFIX_SUCCESS) {
            Safefree(pin);
            infix_arena_destroy(pin->type_arena);
            croak("Internal error: Failed to create pointer type for pin");
        }
        pin->type = void_ptr_type;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(pin));
        SV * rv = newRV_inc(obj_data);
        sv_bless(rv, gv_stashpv("Affix::Pin", GV_ADD));
        sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
        ST(0) = sv_2mortal(rv);
        XSRETURN(1);
    }
    XSRETURN_UNDEF;
}

/*
    void * pointer;              ///< The raw C memory address.
    const infix_type * type;     ///< Infix's description of the data type at 'pointer'. Used for dereferencing.
    infix_arena_t * type_arena;  ///< Memory arena that owns the 'type' structure.
    bool managed;
*/

void _pin(pTHX_ SV * sv, infix_type * type, void * ptr) {
    //~ warn("void _pin(pTHX_ SV * sv, Affix_Type * type = %s, DCpointer ptr = %p) {...", type->stringify.c_str(), ptr);
    MAGIC * mg_;
    Affix_Pin * pin;
    if (SvMAGICAL(sv)) {
        mg_ = mg_findext(sv, PERL_MAGIC_ext, &pin_vtbl_scalar);
        if (mg_ != nullptr) {
            pin = (Affix_Pin *)mg_->mg_ptr;
            if (pin->pointer == nullptr)
                croak("Oh, we messed up");

            warn("[O] Set pointer from %p to %p", pin->pointer, ptr);
            DumpHex(pin->pointer, 16);
            int i = 9999;
            Copy(&i, pin->pointer, 1, int);
            DumpHex(pin->pointer, 16);

            // set_pin(aTHX_ sv, mg_);
            // sv_dump(sv);
            // sv_unmagicext(sv, PERL_MAGIC_ext, &pin_vtbl_scalar);
            // int x = 99999;
            // sv2ptr(aTHX_ type, sv, 1, pin->ptr->address);
            // Copy( ptr, pin->ptr->address,1, int_ptr);
            // pin->ptr->address = & ptr;
            // pin->ptr->address = *(DCpointer*) ptr;
            // return;
        }
    }

    Newxz(pin, 1, Affix_Pin);


    //~ pin = new Affix_Pin(NULL, (Affix_Pointer *)ptr, type);
    warn("[N] Set pointer from %p to %p", pin->pointer, ptr);

    mg_ = sv_magicext(sv, NULL, PERL_MAGIC_ext, &pin_vtbl_scalar, (char *)pin, 0);
    // SvREFCNT_dec(sv);              /* refcnt++ in sv_magicext */
    //~ if (pin->type->depth == 0) {  // Easy to forget to pass a size to Pointer[...]
    //~ pin->type->depth = 1;
    //~ pin->type->length.push_back(1);
    //~ }
}

XS_INTERNAL(Affix_pin) {
    dXSARGS;
    if (items != 4)
        croak_xs_usage(cv, "Affix::pin($var, $lib, $symbol, $type)");
    PING;

    void * symbol = NULL;
    Affix_Lib implicit_lib_handle = NULL;
    SV * target_sv = ST(0);
    SV * name_sv = ST(1);
    PING;

    const char * symbol_name_str = SvPV_nolen(name_sv);

    PING;

    if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
        IV tmp = SvIV((SV *)SvRV(target_sv));
        Affix_Lib lib = INT2PTR(Affix_Lib, tmp);
        symbol = infix_library_get_symbol(lib, symbol_name_str);
    }
    else if (_get_pin_from_sv(aTHX_ target_sv)) {
        Affix_Pin * pin = _get_pin_from_sv(aTHX_ target_sv);
        symbol = pin->pointer;
    }
    else if (!SvOK(target_sv)) {
        implicit_lib_handle = infix_library_open(NULL);
        if (!implicit_lib_handle) {
            XSRETURN_UNDEF;
        }
        symbol = infix_library_get_symbol(implicit_lib_handle, symbol_name_str);
    }
    else {
        const char * path = SvPV_nolen(target_sv);
        implicit_lib_handle = infix_library_open(path);
        if (implicit_lib_handle == NULL) {
            infix_error_details_t err = infix_get_last_error();
            croak("Failed to load library from path '%s': %s", path, err.message);
        }
        symbol = infix_library_get_symbol(implicit_lib_handle, symbol_name_str);
    }

    if (symbol == NULL)
        XSRETURN_UNDEF;

    Affix * affix = nullptr;
    Newxz(affix, 1, Affix);
    affix->lib_handle = implicit_lib_handle;

    const char * signature = SvPV_nolen(ST(2));
    infix_status status = infix_forward_create(&affix->infix, signature, symbol, NULL);
    if (status != INFIX_SUCCESS) {
        if (affix->lib_handle)
            infix_library_close(affix->lib_handle);
        Safefree(affix);
        infix_error_details_t err = infix_get_last_error();
        croak("Failed to create trampoline: %s (code %d, pos %zu)", err.message, err.code, err.position);
    }

    /*
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "type_signature, initial_value");
    const char * signature = SvPV_nolen(ST(0));
    SV * initial_value_sv = ST(1);
    infix_type * type = NULL;
    infix_arena_t * arena = NULL;
    if (infix_type_from_signature(&type, &arena, signature, NULL) != INFIX_SUCCESS)
        croak("Invalid type signature for pin: %s", signature);
    void * ptr = safecalloc(1, infix_type_get_size(type));
    sv2ptr(aTHX_ initial_value_sv, ptr, type);
    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);
    pin->pointer = ptr;
    pin->type = type;
    pin->type_arena = arena;
    pin->managed = true;
    SV * sv_inner = newSV(0);
    sv_setiv(sv_inner, PTR2IV(pin));
    SV * sv_outer = newRV_inc(sv_inner);
    sv_bless(sv_outer, gv_stashpv("Affix::Pin", GV_ADD));
    sv_magicext(sv_inner, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
    ST(0) = sv_2mortal(sv_outer);
    XSRETURN(1);*/
}

/// @brief XS implementation for Affix::sizeof($type_signature).
/// Uses infix's introspection to return the size in bytes of a C type.
XS_INTERNAL(Affix_sizeof) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "type_signature");

    // Get the signature string from the Perl stack.
    const char * signature = SvPV_nolen(ST(0));

    // These will hold the results from the infix parser.
    infix_type * type = NULL;
    infix_arena_t * arena = NULL;

    // Ask infix to parse the signature into a type description.
    // We pass NULL for the registry as we are only dealing with built-in types here.
    // For sizeof(@MyStruct), a registry would need to be passed.
    // NOTE: This could be a future enhancement.
    infix_status status = infix_type_from_signature(&type, &arena, signature, NULL);

    // After parsing, we MUST clean up the arena to avoid memory leaks.
    // A scope guard pattern using a goto is clean and robust in C for this.
    if (status != INFIX_SUCCESS) {
        // The signature was invalid. Croak with a detailed error message.
        infix_error_details_t err = infix_get_last_error();

        // Make sure to clean up the arena even on failure.
        if (arena)
            infix_arena_destroy(arena);

        croak("Invalid type signature for sizeof: '%s' (error at position %zu: %s)",
              signature,
              err.position,
              err.message);
    }

    // On success, get the size from the type object.
    size_t type_size = infix_type_get_size(type);

    // Clean up the memory arena used by the parser.
    infix_arena_destroy(arena);

    // Return the size as a Perl unsigned integer (UV).
    ST(0) = sv_2mortal(newSVuv(type_size));
    XSRETURN(1);
}

// ==============================================================================
// 3. Central Marshalling Functions
// ==============================================================================
void push_sint8(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(int8_t *)p = (int8_t)SvIV(sv);
}
void push_uint8(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint8_t *)p = (uint8_t)SvUV(sv);
}
void push_sint16(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(int16_t *)p = (int16_t)SvIV(sv);
}
void push_uint16(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint16_t *)p = (uint16_t)SvUV(sv);
}
void push_sint32(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(int32_t *)p = (int32_t)SvIV(sv);
}
void push_uint32(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint32_t *)p = (uint32_t)SvUV(sv);
}
void push_sint64(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(int64_t *)p = (int64_t)SvIV(sv);
}
void push_uint64(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint64_t *)p = (uint64_t)SvUV(sv);
}
void push_float(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(float *)p = (float)SvNV(sv);
}
void push_double(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(double *)p = SvNV(sv);
}
void push_long_double(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(long double *)p = (long double)SvNV(sv);
}
void push_bool(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(bool *)p = SvTRUE(sv);
}
#if !defined(INFIX_COMPILER_MSVC)
void push_sint128(pTHX_ const infix_type * t, SV * sv, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
void push_uint128(pTHX_ const infix_type * t, SV * sv, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
#endif

void push_pointer(pTHX_ const infix_type * type, SV * sv, void * ptr) {
    PING;
    if (_get_pin_from_sv(aTHX_ sv)) {
        Affix_Pin * pin = _get_pin_from_sv(aTHX_ sv);
        *(void **)ptr = pin->pointer;
    }
    else if (sv_isobject(sv) && sv_derived_from(sv, "Affix::Callback")) {
        IV tmp = SvIV((SV *)SvRV(sv));
        Affix_Callback * cb = INT2PTR(Affix_Callback *, tmp);
        *(void **)ptr = infix_reverse_get_code(cb->reverse_ctx);
    }
    else if (SvOK(sv)) {
        *(void **)ptr = INT2PTR(void *, SvIV(sv));
    }
    else {
        *(void **)ptr = NULL;
    }
}

//

SV * pull_sint8(pTHX_ const infix_type * t, void * p) {
    return newSViv(*(int8_t *)p);
}
SV * pull_uint8(pTHX_ const infix_type * t, void * p) {
    return newSVuv(*(uint8_t *)p);
}
SV * pull_sint16(pTHX_ const infix_type * t, void * p) {
    return newSViv(*(int16_t *)p);
}
SV * pull_uint16(pTHX_ const infix_type * t, void * p) {
    return newSVuv(*(uint16_t *)p);
}
SV * pull_sint32(pTHX_ const infix_type * t, void * p) {
    return newSViv(*(int32_t *)p);
}
SV * pull_uint32(pTHX_ const infix_type * t, void * p) {
    return newSVuv(*(uint32_t *)p);
}
SV * pull_sint64(pTHX_ const infix_type * t, void * p) {
    return newSViv(*(int64_t *)p);
}
SV * pull_uint64(pTHX_ const infix_type * t, void * p) {
    return newSVuv(*(uint64_t *)p);
}
SV * pull_float(pTHX_ const infix_type * t, void * p) {
    return newSVnv(*(float *)p);
}
SV * pull_double(pTHX_ const infix_type * t, void * p) {
    return newSVnv(*(double *)p);
}
SV * pull_long_double(pTHX_ const infix_type * t, void * p) {
    return newSVnv(*(long double *)p);
}
SV * pull_bool(pTHX_ const infix_type * t, void * p) {
    return newSVbool(*(bool *)p);
}
SV * pull_void(pTHX_ const infix_type * t, void * p) {
    return &PL_sv_undef;
}
#if !defined(INFIX_COMPILER_MSVC)
SV * pull_sint128(pTHX_ const infix_type * t, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
SV * pull_uint128(pTHX_ const infix_type * t, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
#endif

SV * pull_pointer(pTHX_ const infix_type * type, void * ptr) {
    // This is a complex topic. For now, returning a raw integer address is the simplest
    // correct behavior. A future version could auto-create a blessed Affix::Pin.
    void * c_ptr = *(void **)ptr;
    return newSViv(PTR2IV(c_ptr));
}

static const Affix_Push push_handlers[] = {
    [INFIX_PRIMITIVE_BOOL] = push_bool,
    [INFIX_PRIMITIVE_SINT8] = push_sint8,
    [INFIX_PRIMITIVE_UINT8] = push_uint8,
    [INFIX_PRIMITIVE_SINT16] = push_sint16,
    [INFIX_PRIMITIVE_UINT16] = push_uint16,
    [INFIX_PRIMITIVE_SINT32] = push_sint32,
    [INFIX_PRIMITIVE_UINT32] = push_uint32,
    [INFIX_PRIMITIVE_SINT64] = push_sint64,
    [INFIX_PRIMITIVE_UINT64] = push_uint64,
    [INFIX_PRIMITIVE_FLOAT] = push_float,
    [INFIX_PRIMITIVE_DOUBLE] = push_double,
    [INFIX_PRIMITIVE_LONG_DOUBLE] = push_long_double,
#if !defined(INFIX_COMPILER_MSVC)
    [INFIX_PRIMITIVE_SINT128] = push_sint128,
    [INFIX_PRIMITIVE_UINT128] = push_uint128,
#endif
};
static const Affix_Pull pull_handlers[] = {
    [INFIX_PRIMITIVE_BOOL] = pull_bool,
    [INFIX_PRIMITIVE_SINT8] = pull_sint8,
    [INFIX_PRIMITIVE_UINT8] = pull_uint8,
    [INFIX_PRIMITIVE_SINT16] = pull_sint16,
    [INFIX_PRIMITIVE_UINT16] = pull_uint16,
    [INFIX_PRIMITIVE_SINT32] = pull_sint32,
    [INFIX_PRIMITIVE_UINT32] = pull_uint32,
    [INFIX_PRIMITIVE_SINT64] = pull_sint64,
    [INFIX_PRIMITIVE_UINT64] = pull_uint64,
    [INFIX_PRIMITIVE_FLOAT] = pull_float,
    [INFIX_PRIMITIVE_DOUBLE] = pull_double,
    [INFIX_PRIMITIVE_LONG_DOUBLE] = pull_long_double,
#if !defined(INFIX_COMPILER_MSVC)
    [INFIX_PRIMITIVE_SINT128] = pull_sint128,
    [INFIX_PRIMITIVE_UINT128] = pull_uint128,
#endif
};

Affix_Push get_push_handler(const infix_type * type) {
    if (type->category == INFIX_TYPE_PRIMITIVE)
        return push_handlers[type->meta.primitive_id];
    if (type->category == INFIX_TYPE_POINTER)
        return push_pointer;
    return NULL;
}
Affix_Pull get_pull_handler(const infix_type * type) {
    if (type->category == INFIX_TYPE_VOID)
        return pull_void;
    if (type->category == INFIX_TYPE_PRIMITIVE)
        return pull_handlers[type->meta.primitive_id];
    if (type->category == INFIX_TYPE_POINTER)
        return pull_pointer;
    return NULL;
}
void ptr2sv(pTHX_ void * c_ptr, SV * perl_sv, const infix_type * type) {
    Affix_Pull h = get_pull_handler(type);
    if (!h)
        croak("Cannot convert C type to Perl SV: unsupported type");
    SV * new_sv = h(aTHX_ type, c_ptr);
    sv_setsv_mg(perl_sv, new_sv);
}
void sv2ptr(pTHX_ SV * perl_sv, void * c_ptr, const infix_type * type) {
    Affix_Push h = get_push_handler(type);
    if (!h)
        croak("Cannot convert Perl SV to C type: unsupported type");
    h(aTHX_ type, perl_sv, c_ptr);
}

// ==============================================================================
// 4. Core FFI Logic (Forward Calls)
// ==============================================================================
void Affix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    PING;

    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;
    PING;

    if (!affix)
        croak("Internal error: Affix context is NULL in trigger");
    PING;

    size_t num_args = infix_forward_get_num_args(affix->infix);
    PING;

    if ((SP - MARK) != num_args)
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)num_args,
              (UV)(SP - MARK));
    PING;

    affix->args_arena->current_offset = 0;
    void ** c_args = infix_arena_alloc(affix->args_arena, sizeof(void *) * num_args, _Alignof(void *));
    for (size_t i = 0; i < num_args; ++i) {
        PING;

        const infix_type * arg_type = infix_forward_get_arg_type(affix->infix, i);
        void * c_arg_ptr =
            infix_arena_alloc(affix->args_arena, infix_type_get_size(arg_type), infix_type_get_alignment(arg_type));
        affix->push[i](aTHX_ arg_type, ST(i), c_arg_ptr);
        c_args[i] = c_arg_ptr;
        PING;
    }
    PING;

    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    void * ret_ptr =
        infix_arena_alloc(affix->args_arena, infix_type_get_size(ret_type), infix_type_get_alignment(ret_type));
    affix->cif(ret_ptr, c_args);
    PING;

    SV * return_sv = affix->pull(aTHX_ ret_type, ret_ptr);
    ST(0) = sv_2mortal(return_sv);
    XSRETURN(1);
}

XS_INTERNAL(Affix_affix) {
    dXSARGS;
    dXSI32;
    if (items != 3)
        croak_xs_usage(cv, "Affix::affix($target, $name_spec, $signature)");
    PING;

    void * symbol = NULL;
    char * rename = NULL;
    Affix_Lib implicit_lib_handle = NULL;
    SV * target_sv = ST(0);
    SV * name_sv = ST(1);
    PING;

    const char * symbol_name_str = NULL;
    const char * rename_str = NULL;

    if (SvROK(name_sv) && SvTYPE(SvRV(name_sv)) == SVt_PVAV) {
        AV * name_av = (AV *)SvRV(name_sv);
        if (av_count(name_av) != 1)
            croak("Name spec arrayref must contain exactly two elements: [symbol_name, new_sub_name]");
        symbol_name_str = SvPV_nolen(*av_fetch(name_av, 0, 0));
        rename_str = SvPV_nolen(*av_fetch(name_av, 1, 0));
    }
    else {
        symbol_name_str = rename_str = SvPV_nolen(name_sv);
    }
    rename = (char *)rename_str;
    PING;

    if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
        IV tmp = SvIV((SV *)SvRV(target_sv));
        Affix_Lib lib = INT2PTR(Affix_Lib, tmp);
        symbol = infix_library_get_symbol(lib, symbol_name_str);
    }
    else if (_get_pin_from_sv(aTHX_ target_sv)) {
        Affix_Pin * pin = _get_pin_from_sv(aTHX_ target_sv);
        symbol = pin->pointer;
    }
    else if (!SvOK(target_sv)) {
        implicit_lib_handle = infix_library_open(NULL);
        if (!implicit_lib_handle) {
            XSRETURN_UNDEF;
        }
        symbol = infix_library_get_symbol(implicit_lib_handle, symbol_name_str);
    }
    else {
        const char * path = SvPV_nolen(target_sv);
        implicit_lib_handle = infix_library_open(path);
        if (implicit_lib_handle == NULL) {
            infix_error_details_t err = infix_get_last_error();
            croak("Failed to load library from path '%s': %s", path, err.message);
        }
        symbol = infix_library_get_symbol(implicit_lib_handle, symbol_name_str);
    }

    if (symbol == NULL)
        XSRETURN_UNDEF;

    Affix * affix = nullptr;
    Newxz(affix, 1, Affix);
    affix->lib_handle = implicit_lib_handle;

    const char * signature = SvPV_nolen(ST(2));
    infix_status status = infix_forward_create(&affix->infix, signature, symbol, NULL);
    if (status != INFIX_SUCCESS) {
        if (affix->lib_handle)
            infix_library_close(affix->lib_handle);
        Safefree(affix);
        infix_error_details_t err = infix_get_last_error();
        croak("Failed to create trampoline: %s (code %d, pos %zu)", err.message, err.code, err.position);
    }

    affix->cif = infix_forward_get_code(affix->infix);
    size_t num_args = infix_forward_get_num_args(affix->infix);
    Newxz(affix->push, num_args, Affix_Push);
    size_t total_arena_size = sizeof(void *) * num_args;
    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        total_arena_size += infix_type_get_size(type) + infix_type_get_alignment(type);
        affix->push[i] = get_push_handler(type);
        if (affix->push[i] == NULL)
            croak("Unsupported argument type in signature at index %zu", i);
    }
    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    total_arena_size += infix_type_get_size(ret_type) + infix_type_get_alignment(ret_type);
    affix->pull = get_pull_handler(ret_type);
    if (affix->pull == NULL)
        croak("Unsupported return type in signature");

    affix->args_arena = infix_arena_create(total_arena_size);
    char prototype_buf[256] = {0};
    for (size_t i = 0; i < num_args; ++i)
        strcat(prototype_buf, "$");

    CV * cv_new = newXSproto_portable(ix == 0 ? rename : NULL, Affix_trigger, __FILE__, prototype_buf);
    if (UNLIKELY(cv_new == NULL))
        croak("Failed to install new XSUB");
    CvXSUBANY(cv_new).any_ptr = (void *)affix;

    SV * obj = newRV_inc(MUTABLE_SV(cv_new));
    sv_bless(obj, gv_stashpv("Affix", GV_ADD));
    //~ overload_add(gv_stashpv("Affix", GV_ADD), "&{}", newRV_inc(MUTABLE_SV(cv_new)));

    ST(0) = sv_2mortal(obj);
    XSRETURN(1);
}

XS_INTERNAL(Affix_DESTROY) {
    dXSARGS;
    PERL_UNUSED_VAR(items);
    Affix * affix;
    STMT_START {
        HV * st;
        GV * gvp;
        SV * const xsub_tmp_sv = ST(0);
        SvGETMAGIC(xsub_tmp_sv);
        CV * cv_ptr = sv_2cv(xsub_tmp_sv, &st, &gvp, 0);
        affix = (Affix *)CvXSUBANY(cv_ptr).any_ptr;
    }
    STMT_END;
    if (affix != NULL) {
        if (affix->lib_handle != NULL)
            infix_library_close(affix->lib_handle);
        if (affix->args_arena != NULL)
            infix_arena_destroy(affix->args_arena);
        if (affix->infix != NULL)
            infix_forward_destroy(affix->infix);
        if (affix->push != NULL)
            Safefree(affix->push);
        Safefree(affix);
    }
    XSRETURN_EMPTY;
}

void _export_function(pTHX_ HV * _export, const char * what, const char * _tag) {
    SV ** tag = hv_fetch(_export, _tag, strlen(_tag), TRUE);
    if (tag && SvOK(*tag) && SvROK(*tag) && (SvTYPE(SvRV(*tag))) == SVt_PVAV) {
        av_push((AV *)SvRV(*tag), newSVpv(what, 0));
    }
    else {
        AV * av = newAV();
        av_push(av, newSVpv(what, 0));
        (void)hv_store(_export, _tag, strlen(_tag), newRV_noinc(MUTABLE_SV(av)), 0);
    }
}

XS_INTERNAL(Affix_test_multiply) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "a, b");
    int result = SvIV(ST(0)) * SvIV(ST(1));
    ST(0) = sv_2mortal(newSViv(result));
    XSRETURN(1);
}

// ==============================================================================
// 5. Callback (Reverse FFI) Implementation
// ==============================================================================
void _affix_callback_handler_entry(infix_context_t * ctx, void * retval, void ** args) {
    //~ void _affix_callback_handler_entry(infix_context_t * ctx, SV * arg) {
    PING;
    Affix_Callback_Data * cb_data = (Affix_Callback_Data *)infix_reverse_get_user_data(ctx);
    if (!cb_data)
        return;
    dTHXa(cb_data->perl);
    PING;
    dSP;
    PING;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    PING;
    size_t num_args = infix_reverse_get_num_args(ctx);
    PING;
    for (size_t i = 0; i < num_args; ++i) {
        warn("i: %d, num_args: %d", i, num_args);
        PING;
        const infix_type * type = infix_reverse_get_arg_type(ctx, i);
        PING;
        Affix_Pull puller = get_pull_handler(type);
        PING;
        if (!puller)
            croak("Unsupported callback argument type");
        PING;

        XPUSHs(sv_2mortal(puller(aTHX_ type, args[i])));
    }
    PING;


    sv_dump(cb_data->perl_sub);
    PING;
    PUTBACK;
    size_t count = call_sv(SvRV(cb_data->perl_sub), G_SCALAR);
    PING;

    SPAGAIN;
    const infix_type * ret_type = infix_reverse_get_return_type(ctx);
    PING;

    if (ret_type->category != INFIX_TYPE_VOID) {
        PING;

        if (count != 1)
            croak("Callback was expected to return 1 value, but returned %zu", count);
        PING;

        SV * return_sv = POPs;
        PING;

        sv_dump(return_sv);
        PING;
        Affix_Push pusher = get_push_handler(ret_type);
        PING;

        if (pusher == NULL)
            croak("Unsupported callback return type");
        PING;

        pusher(aTHX_ ret_type, return_sv, retval);

        PING;
    }
    PING;

    PUTBACK;
    FREETMPS;
    LEAVE;
    PING;
    return;
}

XS_INTERNAL(Affix_Callback_DESTROY) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "callback_obj");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    Affix_Callback * cb = INT2PTR(Affix_Callback *, tmp);
    if (cb) {
        if (cb->callback_data) {
            SvREFCNT_dec(cb->callback_data->perl_sub);
            Safefree(cb->callback_data);
        }
        if (cb->reverse_ctx) {
            infix_reverse_destroy(cb->reverse_ctx);
        }
        Safefree(cb);
    }
    XSRETURN_EMPTY;
}

XS_INTERNAL(Affix_callback) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$signature, $coderef");
    const char * signature = SvPV_nolen(ST(0));
    SV * coderef = ST(1);
    if (!SvROK(coderef) || SvTYPE(SvRV(coderef)) != SVt_PVCV)
        croak("Second argument must be a code reference");
    PING;
    Affix_Callback_Data * cb_data;
    Newxz(cb_data, 1, Affix_Callback_Data);
    cb_data->perl_sub = newRV_inc(coderef);
    storeTHX(cb_data->perl);
    PING;
    infix_reverse_t * reverse_ctx = NULL;
    infix_status status = infix_reverse_create_closure(
        &reverse_ctx, signature, (void *)_affix_callback_handler_entry, (void *)cb_data, NULL);
    PING;
    if (status != INFIX_SUCCESS) {
        SvREFCNT_dec(cb_data->perl_sub);
        Safefree(cb_data);
        croak("Failed to create callback trampoline");
    }

    Affix_Callback * cb;
    Newxz(cb, 1, Affix_Callback);
    cb->reverse_ctx = reverse_ctx;
    cb->callback_data = cb_data;

    SV * obj_data = newSV(0);
    sv_setiv(obj_data, PTR2IV(cb));
    // Overload the numeric conversion `0+` to return the C function pointer.
    //~ overload_add(gv_stashpv("Affix::Callback", GV_ADD), "0+", newSViv(PTR2IV(infix_reverse_get_code(reverse_ctx))));

    ST(0) = sv_2mortal(sv_bless(newRV_inc(obj_data), gv_stashpv("Affix::Callback", GV_ADD)));
    XSRETURN(1);
}


XS_INTERNAL(Affix_as_string) {
    dVAR;
    dXSARGS;
    PING;
    if (items < 1)
        croak_xs_usage(cv, "$affix");
    {
        char * RETVAL;
        dXSTARG;
        Affix * affix;
        PING;
        if (sv_derived_from(ST(0), "Affix")) {
            PING;
            IV tmp = SvIV((SV *)SvRV(ST(0)));
            affix = INT2PTR(Affix *, tmp);
            PING;
        }
        else
            croak("affix is not of type Affix");
        PING;
        RETVAL = (char *)affix->infix->target_fn;
        PING;
        sv_setpv(TARG, RETVAL);
        XSprePUSH;
        PUSHTARG;
        PING;
    }
    PING;
    XSRETURN(1);
};

XS_INTERNAL(Affix_END) {
    dXSARGS;
    dMY_CXT;
    PERL_UNUSED_VAR(items);

    if (MY_CXT.lib_registry) {
        hv_iterinit(MY_CXT.lib_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.lib_registry))) {
            SV * entry_sv = HeVAL(he);
            LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
            if (entry) {
                if (entry->lib) {
                    infix_library_close(entry->lib);
                }
                Safefree(entry);
            }
        }
        hv_undef(MY_CXT.lib_registry);  // Frees the hash itself
        MY_CXT.lib_registry = NULL;     // Guards against double-free
    }
    XSRETURN_EMPTY;
}


// Debugging
void _DumpHex(pTHX_ const void * addr, size_t len, const char * file, int line) {
    if (addr == nullptr) {
        printf("Dumping %lu bytes from null pointer %p at %s line %d\n", len, addr, file, line);
        fflush(stdout);
        return;
    }
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
    safefree(buff);
    fflush(stdout);
}

#define DD(scalar) _DD(aTHX_ scalar, __FILE__, __LINE__)
void _DD(pTHX_ SV * scalar, const char * file, int line) {
    Perl_load_module(aTHX_ PERL_LOADMOD_NOIMPORT, newSVpvs("Data::Printer"), NULL, NULL, NULL);
    if (!get_cvs("Data::Printer::p", GV_NOADD_NOINIT | GV_NO_SVGMAGIC))
        return;

    fflush(stdout);
    dSP;
    int count;

    ENTER;
    SAVETMPS;

    PUSHMARK(SP);
    EXTEND(SP, 1);
    PUSHs(scalar);
    PUTBACK;

    count = call_pv("Data::Printer::p", G_SCALAR);

    SPAGAIN;

    if (count != 1)
        croak("Big trouble\n");

    STRLEN len;
    const char * s = SvPVx(POPs, len);

    printf("%s at %s line %d\n", s, file, line);
    fflush(stdout);

    PUTBACK;
    FREETMPS;
    LEAVE;
}
// ==============================================================================
// 6. XS Bootstrap
// ==============================================================================
void boot_Affix(pTHX_ CV * cv) {
    dXSBOOTARGSXSAPIVERCHK;
    PERL_UNUSED_VAR(items);
#ifdef USE_ITHREADS
    my_perl = (PerlInterpreter *)PERL_GET_CONTEXT;
#endif

    MY_CXT_INIT;
    MY_CXT.lib_registry = newHV();

    newXS("Affix::END", Affix_END, __FILE__);


    newXS("Affix::affix", Affix_affix, __FILE__);
    newXS("Affix::wrap", Affix_affix, __FILE__);
    newXS("Affix::DESTROY", Affix_DESTROY, __FILE__);


    //(void)newXSproto_portable("Affix::Type::Pointer::(|", Affix_Type_Pointer, __FILE__, "");
    /* The magic for overload gets a GV* via gv_fetchmeth as */
    /* mentioned above, and looks in the SV* slot of it for */
    /* the "fallback" status. */
    sv_setsv(get_sv("Affix::()", TRUE), &PL_sv_yes);
    /* Making a sub named "Affix::Pointer::()" allows the package */
    /* to be findable via fetchmethod(), and causes */
    /* overload::Overloaded("Affix::Pointer") to return true. */
    // (void)newXS_deffile("Affix::Pointer::()", Affix_Pointer_as_string);
    (void)newXSproto_portable("Affix::()", Affix_as_string, __FILE__, "$;@");
    //(void)newXSproto_portable("Affix::(\"\"", Affix_Pointer_as_string, __FILE__, "$;@");
    //(void)newXSproto_portable("Affix::as_string", Affix_Pointer_as_string, __FILE__, "$;@");
    //(void)newXSproto_portable("Affix::(%{}", Affix_Pointer_deref_hash, __FILE__, "$;@");
    //(void)newXSproto_portable("Affix::(@{}", Affix_Pointer_deref_list, __FILE__, "$;@");
    //  ${}  @{}  %{}  &{}  *{}


    newXS("Affix::load_library", Affix_load_library, __FILE__);
    sv_setsv(get_sv("Affix::Lib::()", TRUE), &PL_sv_yes);
    (void)newXSproto_portable("Affix::Lib::(0+", Affix_Lib_as_string, __FILE__, "$;@");
    (void)newXSproto_portable("Affix::Lib::()", Affix_as_string, __FILE__, "$;@");
    newXS("Affix::Lib::DESTROY", Affix_Lib_DESTROY, __FILE__);
    newXS("Affix::find_symbol", Affix_find_symbol, __FILE__);
    newXS("Affix::get_last_error_message", Affix_get_last_error_message, __FILE__);
    (void)newXSproto_portable("Affix::pin", Affix_pin, __FILE__, "$$$$");
    newXS("Affix::test_multiply", Affix_test_multiply, __FILE__);
    newXS("Affix::sizeof", Affix_sizeof, __FILE__);
    newXS("Affix::callback", Affix_callback, __FILE__);
    newXS("Affix::Callback::DESTROY", Affix_Callback_DESTROY, __FILE__);

    export_function("Affix", "sizeof", "core");  // Add to the :core tag
    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");
    export_function("Affix", "load_library", "lib");
    export_function("Affix", "find_symbol", "lib");
    export_function("Affix", "get_last_error_message", "core");
    export_function("Affix", "pin", "pin");
    export_function("Affix", "callback", "callback");

    Perl_xs_boot_epilog(aTHX_ ax);
}
