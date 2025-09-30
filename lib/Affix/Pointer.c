/**
 * @file Pointer.c
 * @brief Implements the Affix::Pointer object and memory management functions.
 *
 * @details This file provides the XS implementation for the `Affix::Pointer`
 * Perl class. This class is a simple wrapper around a raw C `void*` pointer,
 * allowing Perl code to interact with memory allocated by C.
 *
 * It also exports a suite of functions that mirror the standard C library's
 * memory management functions (malloc, calloc, free, memcpy, etc.), making
 * them directly available to Perl scripts. This functionality is independent of
 * the FFI call mechanism and remains largely unchanged in the refactoring.
 *
 * The key change is the removal of the old `store` and `fetch` methods, as
 * data marshalling is now handled by a new, centralized system that uses
 * `infix`'s `ffi_type` descriptors.
 */

#include "../Affix.h"
#include <string.h>  // For memcpy, etc.

/**
 * @brief Safely gets the internal Affix_Pointer struct from a blessed SV.
 * @details Croaks if the SV is not a valid Affix::Pointer object. This is the
 *          primary entry point for all methods operating on a pointer object.
 * @param sv The Perl SV to inspect.
 * @return A pointer to the Affix_Pointer struct, or NULL if the object was freed.
 */
static inline Affix_Pointer * get_pointer_struct(pTHX_ SV * sv) {
    if (!sv_isobject(sv) || !sv_derived_from(sv, "Affix::Pointer"))
        croak("Argument is not a valid Affix::Pointer object");
    IV ptr_iv = SvIV(SvRV(sv));
    if (!ptr_iv)  // Already free?
        return NULL;
    return INT2PTR(Affix_Pointer *, ptr_iv);
}

/**
 * @brief A flexible helper to get a raw `void*` from an SV.
 * @details It can extract the address from an Affix::Pointer, a pinned variable,
 *          a raw integer/IV (representing an address), or a Perl string (for source buffers).
 *          It croaks if the conversion is not possible.
 * @param sv The Perl SV to convert.
 * @return The raw `void*` address.
 */
static void * sv_to_voidp(pTHX_ SV * sv) {
    if (sv_isobject(sv) && sv_derived_from(sv, "Affix::Pointer")) {
        Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ sv);
        return ptr_struct ? ptr_struct->address : NULL;
    }
    if (is_pin(aTHX_ sv)) {
        Affix_Pin * pin = get_pin(aTHX_ sv);
        return pin ? pin->pointer : NULL;
    }

    if (SvIOK(sv))
        return INT2PTR(void *, SvIV(sv));

    if (SvPOK(sv))
        return (void *)SvPV_nolen(sv);

    croak("Cannot convert argument to a C pointer");
    return NULL;  // Unreachable
}

/**
 * @brief XSUB for `Affix::Pointer->new($type_signature, $count)`.
 * @details The primary constructor for creating managed memory buffers. It
 *          allocates enough memory for `count` elements of the type described
 *          by the signature.
 */
XS_INTERNAL(Affix_Pointer_new) {
    dXSARGS;
    // We are called as a class method, so the class name is ST(0).
    if (items != 3)
        croak_xs_usage(cv, "klass, $type_signature, $count");

    const char * signature = SvPV_nolen(ST(1));
    size_t count = SvUV(ST(2));

    arena_t * type_arena = NULL;
    ffi_type * type = NULL;

    ffi_status status = ffi_type_from_signature(&type, &type_arena, signature);
    if (status != FFI_SUCCESS || !type) {
        if (type_arena)
            arena_destroy(type_arena);
        croak("Failed to parse type signature for Affix::Pointer->new: '%s'", signature);
    }
    if (type->size == 0) {  // e.g. for 'void'
        arena_destroy(type_arena);
        croak("Cannot create a pointer to a type of size 0");
    }

    size_t total_bytes = type->size * count;

    void * ptr = safecalloc(count, type->size);
    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = ptr;
    ptr_struct->managed = 1;  // It's our problem now
    ptr_struct->type = type;
    ptr_struct->type_arena = type_arena;
    ptr_struct->count = count;
    ptr_struct->position = 0;

    // Canonical object creation
    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer", GV_ADD));

    XSRETURN(1);
}

/**
 * @brief Method `->managed([$bool])`: Gets or sets the managed flag.
 * @details If called with an argument, it sets the flag. It always returns the
 *          current (possibly new) state of the flag.
 */
XS_INTERNAL(Affix_Pointer_managed) {
    dXSARGS;
    if (items < 1 || items > 2)
        croak_xs_usage(cv, "$self, [$new_value]");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct)
        XSRETURN_UNDEF;  // Cannot operate on a freed pointer

    if (items == 2)
        ptr_struct->managed = SvTRUE(ST(1));

    ST(0) = newSVbool(ptr_struct->managed);
    XSRETURN(1);
}

/**
 * @brief XSUB for `Affix::Pointer->cast($new_type_signature)`.
 * @details Creates a new, unmanaged Affix::Pointer that is a typed "view"
 *          into the same memory as the original pointer.
 * @return A new `Affix::Pointer::Unmanaged` object.
 */
XS_INTERNAL(Affix_Pointer_cast) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$self, $new_type_signature");

    Affix_Pointer * original_ptr = get_pointer_struct(aTHX_ ST(0));
    if (!original_ptr || !original_ptr->address)
        croak("Cannot cast a freed or null pointer");

    if (!original_ptr->type || original_ptr->type->size == 0)
        croak("Cannot cast a pointer whose original type has size 0 (e.g., void*)");

    const char * new_signature = SvPV_nolen(ST(1));
    arena_t * new_arena = NULL;
    ffi_type * new_type = NULL;
    ffi_status status = ffi_type_from_signature(&new_type, &new_arena, new_signature);

    if (status != FFI_SUCCESS || !new_type) {
        if (new_arena)
            arena_destroy(new_arena);
        croak("Failed to parse new type signature for cast: '%s'", new_signature);
    }
    if (new_type->size == 0) {
        arena_destroy(new_arena);
        croak("Cannot cast to a type of size 0 (e.g., void)");
    }

    // Calculate total size of the original buffer to determine the new count.
    size_t total_bytes = original_ptr->count * original_ptr->type->size;
    size_t new_count = (new_type->size > 0) ? (total_bytes / new_type->size) : 0;

    // Create the new wrapper struct for the casted view.
    Affix_Pointer * casted_ptr;
    Newxz(casted_ptr, 1, Affix_Pointer);

    casted_ptr->address = original_ptr->address;  // Points to the SAME memory
    casted_ptr->managed = 0;  // IMPORTANT: The casted pointer is a VIEW and does not own the memory.
    casted_ptr->type = new_type;
    casted_ptr->type_arena = new_arena;  // The new object owns the new arena.
    casted_ptr->count = new_count;
    casted_ptr->position = 0;  // Reset position for the new view

    // Bless into the Unmanaged class to reflect its ownership status.
    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(casted_ptr));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer::Unmanaged", GV_ADD));

    XSRETURN(1);
}

/**
 * @brief XSUB for `Affix::malloc($size)`. Allocates raw, untyped memory.
 * @return A new, managed `Affix::Pointer` object.
 */
XS_INTERNAL(Affix_Pointer_malloc) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$size");
    size_t size = SvUV(ST(0));
    void * ptr = safemalloc(size);
    if (!ptr)
        XSRETURN_EMPTY;

    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = ptr;
    ptr_struct->managed = 1;
    ptr_struct->type = ffi_type_create_primitive(FFI_PRIMITIVE_TYPE_UINT8);  // Treat as bytes
    ptr_struct->type_arena = NULL;                                           // Type is static, no arena needed
    ptr_struct->count = size;
    ptr_struct->position = 0;

    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer", GV_ADD));

    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::calloc($num, $size)`. Allocates zeroed, untyped memory.
 * @return A new, managed `Affix::Pointer` object.
 */
XS_INTERNAL(Affix_Pointer_calloc) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$num, $size");
    size_t num = SvUV(ST(0));
    size_t size = SvUV(ST(1));
    void * ptr = safecalloc(num, size);
    if (!ptr)
        XSRETURN_EMPTY;

    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = ptr;
    ptr_struct->managed = 1;
    ptr_struct->type = ffi_type_create_primitive(FFI_PRIMITIVE_TYPE_UINT8);
    ptr_struct->type_arena = NULL;
    ptr_struct->count = num * size;
    ptr_struct->position = 0;

    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer", GV_ADD));

    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::realloc($pointer, $new_size)`.
 * @return The same `Affix::Pointer` object, now pointing to the reallocated memory.
 */
XS_INTERNAL(Affix_Pointer_realloc) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$ptr, $new_size");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct)
        croak("Cannot realloc a freed or invalid Affix::Pointer");
    if (!ptr_struct->managed)
        croak("Cannot realloc an unmanaged Affix::Pointer");

    size_t new_size = SvUV(ST(1));
    void * new_ptr = saferealloc(ptr_struct->address, new_size);
    if (!new_ptr)
        croak("realloc failed");

    ptr_struct->address = new_ptr;
    // If pointer is typed, count is based on type size
    ptr_struct->count = ptr_struct->type->size > 0 ? new_size / ptr_struct->type->size : new_size;

    //~ ST(0) = ST(0);  // Return the same object
    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::free($pointer)`. Frees memory owned by a managed pointer.
 */
XS_INTERNAL(Affix_Pointer_free) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$ptr");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (ptr_struct) {
        if (ptr_struct->managed && ptr_struct->address) {
            safefree(ptr_struct->address);
            ptr_struct->address = NULL;  // Prevent use-after-free
        }
        else if (!ptr_struct->managed) {
            Perl_warn(aTHX_ "Affix::free called on an unmanaged pointer; memory was not freed.");
        }
    }
    XSRETURN_EMPTY;
}
/**
 * @brief XSUB for `Affix::memcpy($dest, $src, $count)`.
 */
XS_INTERNAL(Affix_Pointer_memcpy) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$dest, $src, $count");
    void * dest = sv_to_voidp(aTHX_ ST(0));
    void * src = sv_to_voidp(aTHX_ ST(1));
    size_t count = SvUV(ST(2));

    if (dest && src)
        memcpy(dest, src, count);

    //~ ST(0) = ST(0);  // Return dest
    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::memmove($dest, $src, $count)`.
 */
XS_INTERNAL(Affix_Pointer_memmove) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$dest, $src, $count");
    void * dest = sv_to_voidp(aTHX_ ST(0));
    void * src = sv_to_voidp(aTHX_ ST(1));
    size_t count = SvUV(ST(2));

    if (dest && src)
        memmove(dest, src, count);

    //~ ST(0) = ST(0);  // Return dest
    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::memset($ptr, $char, $count)`.
 */
XS_INTERNAL(Affix_Pointer_memset) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$ptr, $char, $count");
    void * ptr = sv_to_voidp(aTHX_ ST(0));
    int c = SvIV(ST(1));
    size_t count = SvUV(ST(2));

    if (ptr)
        memset(ptr, c, count);

    //~ ST(0) = ST(0);  // Return ptr
    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::memcmp($ptr1, $ptr2, $count)`.
 */
XS_INTERNAL(Affix_Pointer_memcmp) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$ptr1, $ptr2, $count");
    void * ptr1 = sv_to_voidp(aTHX_ ST(0));
    void * ptr2 = sv_to_voidp(aTHX_ ST(1));
    size_t count = SvUV(ST(2));

    if (ptr1 && ptr2) {
        ST(0) = newSViv(memcmp(ptr1, ptr2, count));
    }
    else {
        ST(0) = &PL_sv_undef;
    }
    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::memchr($ptr, $char, $count)`.
 * @return A new, unmanaged `Affix::Pointer` to the found character.
 */
XS_INTERNAL(Affix_Pointer_memchr) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$ptr, $char, $count");
    void * ptr = sv_to_voidp(aTHX_ ST(0));
    int c = SvIV(ST(1));
    size_t count = SvUV(ST(2));

    if (!ptr)
        XSRETURN_UNDEF;

    void * result_ptr = memchr(ptr, c, count);
    if (!result_ptr)
        XSRETURN_UNDEF;

    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = result_ptr;
    ptr_struct->managed = 0;  // This is a VIEW, do not free it.
    ptr_struct->type = ffi_type_create_primitive(FFI_PRIMITIVE_TYPE_UINT8);
    ptr_struct->type_arena = NULL;
    ptr_struct->count = 1;  // It points to one char
    ptr_struct->position = 0;

    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer::Unmanaged", GV_ADD));

    XSRETURN(1);
}
/**
 * @brief XSUB for `Affix::strdup($string_or_ptr)`.
 * @return A new, managed `Affix::Pointer` containing the duplicated string.
 */
XS_INTERNAL(Affix_Pointer_strdup) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$string_or_ptr");
    const char * str = SvPV_nolen(ST(0));
    char * new_str = strdup(str);  // Uses standard C malloc
    if (!new_str)
        XSRETURN_EMPTY;

    // The result of strdup IS a new allocation that we should manage.
    Affix_Pointer * ptr_struct;
    Newxz(ptr_struct, 1, Affix_Pointer);
    ptr_struct->address = new_str;
    ptr_struct->managed = 1;  // We own this memory
    ptr_struct->type = ffi_type_create_primitive(FFI_PRIMITIVE_TYPE_SINT8);
    ptr_struct->type_arena = NULL;
    ptr_struct->count = strlen(new_str) + 1;
    ptr_struct->position = 0;

    SV * self_sv = newSV(0);
    sv_setiv(self_sv, PTR2IV(ptr_struct));
    ST(0) = sv_bless(newRV_noinc(self_sv), gv_stashpv("Affix::Pointer::Unmanaged", GV_ADD));

    XSRETURN(1);
}

/**
 * @brief Method `->raw($count)`: Returns raw bytes from the pointer.
 */
XS_INTERNAL(Affix_Pointer_raw) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$self, $count");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (ptr_struct && ptr_struct->address) {
        ST(0) = newSVpvn((const char *)ptr_struct->address, SvIV(ST(1)));
    }
    else {
        ST(0) = &PL_sv_undef;
    }
    XSRETURN(1);
}

/**
 * @brief Method `->dump($count)`: Dumps hex representation of memory.
 */
XS_INTERNAL(Affix_Pointer_dump) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$self, $count");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (ptr_struct && ptr_struct->address) {
        _DumpHex(aTHX_ ptr_struct->address, SvIV(ST(1)), CopFILE(PL_curcop), CopLINE(PL_curcop));
    }
    XSRETURN_EMPTY;
}

/**
 * @brief Overloaded stringification for `""`.
 */
XS_INTERNAL(Affix_Pointer_as_string) {
    dXSARGS;
    warn("Affix_Pointer_as_string/items: %d", items);
    if (items != 1)
        croak_xs_usage(cv, "pointer");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    const char * class_name = sv_reftype(SvRV(ST(0)), TRUE);
    void * ptr = ptr_struct ? ptr_struct->address : NULL;
    ST(0) = sv_2mortal(Perl_newSVpvf(aTHX_ "%s(0x%" UVxf ")", class_name, PTR2UV(ptr)));
    XSRETURN(1);
}
/**
 * @brief XSUB for array element access: `$ptr->[$index]`
 */
XS_INTERNAL(Affix_Pointer_FETCH) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$self, $index");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address)
        XSRETURN_UNDEF;

    IV index = SvIV(ST(1));
    if (index < 0 || (size_t)index >= ptr_struct->count) {
        warn("Index %" IVdf " out of bounds for Affix::Pointer (count: %" UVuf ")", index, (UV)ptr_struct->count);
        XSRETURN_UNDEF;
    }

    void * elem_addr = (char *)ptr_struct->address + (index * ptr_struct->type->size);
    ST(0) = fetch_c_to_sv(aTHX_ elem_addr, ptr_struct->type);
    XSRETURN(1);
}
/**
 * @brief Method `->get($index)`: Returns the value at a given element index.
 */
XS_INTERNAL(Affix_Pointer_get) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "$self, $index");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address)
        XSRETURN_UNDEF;

    IV index = SvIV(ST(1));
    if (index < 0 || (size_t)index >= ptr_struct->count) {
        Perl_warn(
            aTHX_ "Index %" IVdf " out of bounds for Affix::Pointer (count: %" UVuf ")", index, (UV)ptr_struct->count);
        XSRETURN_UNDEF;
    }

    void * elem_addr = (char *)ptr_struct->address + (index * ptr_struct->type->size);
    ST(0) = fetch_c_to_sv(aTHX_ elem_addr, ptr_struct->type);
    XSRETURN(1);
}

/**
 * @brief XSUB for array element assignment: `$ptr->[$index] = $value`
 */
XS_INTERNAL(Affix_Pointer_STORE) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$self, $index, $value");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address)
        XSRETURN_EMPTY;

    IV index = SvIV(ST(1));
    if (index < 0 || (size_t)index >= ptr_struct->count) {
        croak("Index %" IVdf " out of bounds for Affix::Pointer (count: %" UVuf ")", index, (UV)ptr_struct->count);
    }

    void * elem_addr = (char *)ptr_struct->address + (index * ptr_struct->type->size);
    marshal_sv_to_c(aTHX_ elem_addr, ST(2), ptr_struct->type);
    XSRETURN_EMPTY;
}

/**
 * @brief Method `->set($index, $value)`: Sets the value at a given element index.
 */
XS_INTERNAL(Affix_Pointer_set) {
    dXSARGS;
    if (items != 3)
        croak_xs_usage(cv, "$self, $index, $value");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address)
        XSRETURN_EMPTY;

    IV index = SvIV(ST(1));
    if (index < 0 || (size_t)index >= ptr_struct->count) {
        croak("Index %" IVdf " out of bounds for Affix::Pointer (count: %" UVuf ")", index, (UV)ptr_struct->count);
    }

    void * elem_addr = (char *)ptr_struct->address + (index * ptr_struct->type->size);
    marshal_sv_to_c(aTHX_ elem_addr, ST(2), ptr_struct->type);
    XSRETURN_EMPTY;
}

/**
 * @brief XSUB for scalar dereference: `${$ptr}`
 * @details Reads the value at the current `position`.
 */
XS_INTERNAL(Affix_Pointer_deref_scalar) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$self");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address)
        XSRETURN_UNDEF;

    if (ptr_struct->position >= ptr_struct->count) {
        Perl_warn(aTHX_ "Position %" UVuf " out of bounds for Affix::Pointer (count: %" UVuf ")",
                  (UV)ptr_struct->position,
                  (UV)ptr_struct->count);
        XSRETURN_UNDEF;
    }

    void * elem_addr = (char *)ptr_struct->address + (ptr_struct->position * ptr_struct->type->size);
    ST(0) = fetch_c_to_sv(aTHX_ elem_addr, ptr_struct->type);
    XSRETURN(1);
}

/**
 * @brief Overloaded array dereference: `@{ $pointer }`
 * @details Treats the C pointer as an array of `count` elements and returns a
 *          new Perl array reference containing the marshalled values.
 */
XS_INTERNAL(Affix_Pointer_deref_list) {
    dXSARGS;
    warn("Line %d", __LINE__);
    if (items != 1)
        croak_xs_usage(cv, "$self");
    warn("ARRAY!!!!!!!!!!!!!!!!!!!!!!!!");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address) {
        ST(0) = newRV_noinc(MUTABLE_SV(newAV()));  // Return empty arrayref
        XSRETURN(1);
    }

    AV * av = newAV();
    ffi_type * elem_type = ptr_struct->type;

    for (size_t i = 0; i < ptr_struct->count; ++i) {
        void * elem_src = (char *)ptr_struct->address + (i * elem_type->size);
        SV * elem_sv = fetch_c_to_sv(aTHX_ elem_src, elem_type);
        av_push(av, elem_sv);
    }

    ST(0) = sv_2mortal(newRV_noinc(MUTABLE_SV(av)));
    XSRETURN(1);
}

/**
 * @brief Overloaded hash dereference: `%{ $pointer }`
 * @details Treats the C pointer as a pointer to a struct. It reads the struct's
 *          members and returns a new Perl hash reference mapping member names
 *          to their marshalled values.
 */
XS_INTERNAL(Affix_Pointer_deref_hash) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$self");

    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (!ptr_struct || !ptr_struct->address) {
        ST(0) = newRV_noinc(MUTABLE_SV(newHV()));  // Return empty hashref
        XSRETURN(1);
    }

    // This operation is only valid if the pointer is typed as a struct.
    if (ptr_struct->type->category != FFI_TYPE_STRUCT)
        croak("Cannot dereference pointer as a hash unless it is typed as a struct");

    HV * hash = newHV();
    for (size_t i = 0; i < ptr_struct->type->meta.aggregate_info.num_members; ++i) {
        ffi_struct_member * affix_member = &ptr_struct->type->meta.aggregate_info.members[i];

        // A member must have a name to be a hash key.
        if (affix_member->name) {
            void * member_src = (char *)ptr_struct->address + affix_member->offset;
            SV * val_sv = fetch_c_to_sv(aTHX_ member_src, affix_member->type);
            hv_store(hash, affix_member->name, strlen(affix_member->name), val_sv, 0);
        }
    }

    ST(0) = sv_2mortal(newRV_noinc(MUTABLE_SV(hash)));
    XSRETURN(1);
}

/**
 * @brief XSUB for the `++` operator.
 */
XS_INTERNAL(Affix_Pointer_inc) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$self");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (ptr_struct) {
        ptr_struct->position++;
    }
    // The operator should return the object itself.
    ST(0) = ST(0);
    XSRETURN(1);
}

/**
 * @brief XSUB for the `--` operator.
 */
XS_INTERNAL(Affix_Pointer_dec) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "$self");
    Affix_Pointer * ptr_struct = get_pointer_struct(aTHX_ ST(0));
    if (ptr_struct && ptr_struct->position > 0) {
        ptr_struct->position--;
    }
    ST(0) = ST(0);
    XSRETURN(1);
}

/**
 * @brief The DESTROY method for Affix::Pointer objects.
 */
XS_INTERNAL(Affix_Pointer_DESTROY) {
    dXSARGS;
    if (items != 1)
        return;
    IV ptr_iv = SvIV(SvRV(ST(0)));
    if (!ptr_iv)
        return;  // Already destroyed
    Affix_Pointer * ptr_struct = INT2PTR(Affix_Pointer *, ptr_iv);

    if (ptr_struct->managed && ptr_struct->address)
        safefree(ptr_struct->address);  // Free the C memory if we own it

    if (ptr_struct->type_arena)
        arena_destroy(ptr_struct->type_arena);

    safefree(ptr_struct);  // Free the wrapper struct itself

    sv_setiv(SvRV(ST(0)), 0);
    XSRETURN_EMPTY;
}
/**
 * @brief DESTROY for unmanaged `Affix::Pointer::Unmanaged` objects.
 * @details This is identical to the managed DESTROY, except it does NOT free
 *          the `address` member, as it does not own that memory.
 */
XS_INTERNAL(Affix_Pointer_Unmanaged_DESTROY) {
    dXSARGS;
    if (items != 1)
        return;
    IV ptr_iv = SvIV(SvRV(ST(0)));
    if (!ptr_iv)
        return;
    Affix_Pointer * ptr_struct = INT2PTR(Affix_Pointer *, ptr_iv);

    // DO NOT free ptr_struct->address, as we do not own it.

    if (ptr_struct->type_arena) {
        arena_destroy(ptr_struct->type_arena);
    }
    safefree(ptr_struct);  // Free the wrapper struct itself
    sv_setiv(SvRV(ST(0)), 0);
    XSRETURN_EMPTY;
}

// --- XS Boot Section ---

void boot_Affix_Pointer(pTHX_ CV * cv) {
    PERL_UNUSED_VAR(cv);

    // Memory allocation is now primarily done via the constructor
    (void)newXSproto_portable("Affix::Pointer::new", Affix_Pointer_new, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::Pointer::free", Affix_Pointer_free, __FILE__, "$");
    (void)newXSproto_portable("Affix::Pointer::dump", Affix_Pointer_dump, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::raw", Affix_Pointer_raw, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::cast", Affix_Pointer_cast, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::get", Affix_Pointer_get, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::set", Affix_Pointer_set, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::Pointer::managed", Affix_Pointer_managed, __FILE__, "$;$");

    // Core object methods
    (void)newXSproto_portable("Affix::Pointer::DESTROY", Affix_Pointer_DESTROY, __FILE__, "$");
    (void)newXSproto_portable("Affix::Pointer::Unmanaged::DESTROY", Affix_Pointer_Unmanaged_DESTROY, __FILE__, "$");

    // Raw memory functions
    (void)newXSproto_portable("Affix::malloc", Affix_Pointer_malloc, __FILE__, "$");
    (void)newXSproto_portable("Affix::calloc", Affix_Pointer_calloc, __FILE__, "$$");
    (void)newXSproto_portable("Affix::realloc", Affix_Pointer_realloc, __FILE__, "$$");
    (void)newXSproto_portable("Affix::free", Affix_Pointer_free, __FILE__, "$");
    (void)newXSproto_portable("Affix::memcpy", Affix_Pointer_memcpy, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::memmove", Affix_Pointer_memmove, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::memset", Affix_Pointer_memset, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::memcmp", Affix_Pointer_memcmp, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::memchr", Affix_Pointer_memchr, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::strdup", Affix_Pointer_strdup, __FILE__, "$");
    export_function("Affix", "malloc", "memory");
    export_function("Affix", "calloc", "memory");
    export_function("Affix", "realloc", "memory");
    export_function("Affix", "free", "memory");
    export_function("Affix", "memcpy", "memory");
    export_function("Affix", "memmove", "memory");
    export_function("Affix", "memset", "memory");
    export_function("Affix", "memcmp", "memory");
    export_function("Affix", "memchr", "memory");
    export_function("Affix", "strdup", "memory");

    // Overloaded operators
    /* The magic for overload gets a GV* via gv_fetchmeth as */
    /* mentioned above, and looks in the SV* slot of it for */
    /* the "fallback" status. */
    sv_setsv(get_sv("Affix::Pointer::()", TRUE), &PL_sv_yes);
    /* Making a sub named "Affix::Pointer::()" allows the package */
    /* to be findable via fetchmethod(), and causes */
    /* overload::Overloaded("Affix::Pointer") to return true. */
    //~ (void)newXSproto_portable("Affix::Pointer::()", Affix_Pointer_as_string, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::FETCH", Affix_Pointer_FETCH, __FILE__, "$$");
    (void)newXSproto_portable("Affix::Pointer::STORE", Affix_Pointer_STORE, __FILE__, "$$$");
    (void)newXSproto_portable("Affix::Pointer::(\"\"", Affix_Pointer_as_string, __FILE__, "$;@");
    (void)newXSproto_portable("Affix::Pointer::as_string", Affix_Pointer_as_string, __FILE__, "$;@");
    //~ (void)newXSproto_portable("Affix::Pointer::(${}", Affix_Pointer_deref_scalar, __FILE__, "$");
    //~ (void)newXSproto_portable("Affix::Pointer::(%{}", Affix_Pointer_deref_hash, __FILE__, "$;@");
    //~ (void)newXSproto_portable("Affix::Pointer::(@{}", Affix_Pointer_deref_list, __FILE__, "$;@");
    (void)newXSproto_portable("Affix::Pointer::(\"++\")", Affix_Pointer_inc, __FILE__, "$");
    (void)newXSproto_portable("Affix::Pointer::(\"--\")", Affix_Pointer_dec, __FILE__, "$");

    // Set up inheritance
    set_isa("Affix::Pointer::Unmanaged", "Affix::Pointer");
}
