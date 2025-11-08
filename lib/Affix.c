#include "Affix.h"
#include <string.h>

// Forward declarations
SV * pull_struct(pTHX_ const infix_type *, void *);
SV * pull_union(pTHX_ const infix_type *, void *);
SV * pull_array(pTHX_ const infix_type *, void *);
void push_reverse_trampoline(pTHX_ const infix_type *, SV *, void *);
SV * pull_reverse_trampoline(pTHX_ const infix_type *, void *);
SV * pull_enum(pTHX_ const infix_type *, void *);
SV * pull_complex(pTHX_ const infix_type *, void * p);
SV * pull_vector(pTHX_ const infix_type *, void * p);

static SV * _format_parse_error(pTHX_ const char * context_msg, const char * signature, infix_error_details_t err) {
    STRLEN sig_len = strlen(signature);
    int radius = 20;
    size_t start = (err.position > radius) ? (err.position - radius) : 0;
    size_t end = (err.position + radius < sig_len) ? (err.position + radius) : sig_len;
    const char * start_indicator = (start > 0) ? "... " : "";
    const char * end_indicator = (end < sig_len) ? " ..." : "";
    int start_indicator_len = (start > 0) ? 4 : 0;
    char snippet[128];
    snprintf(
        snippet, sizeof(snippet), "%s%.*s%s", start_indicator, (int)(end - start), signature + start, end_indicator);
    char pointer[128];
    int caret_pos = err.position - start + start_indicator_len;
    snprintf(pointer, sizeof(pointer), "%*s^", caret_pos, "");
    return sv_2mortal(newSVpvf("Failed to parse signature %s:\n\n  %s\n  %s\n\nError: %s (at position %zu)",
                               context_msg,
                               snippet,
                               pointer,
                               err.message,
                               err.position));
}
XS_INTERNAL(Affix_Lib_as_string) {
    dVAR;
    dXSARGS;
    if (items < 1)
        croak_xs_usage(cv, "$lib");
    IV RETVAL;
    {
        infix_library_t * lib;
        IV tmp = SvIV((SV *)SvRV(ST(0)));
        lib = INT2PTR(infix_library_t *, tmp);
        RETVAL = PTR2IV(lib->handle);
    }
    XSRETURN_IV(RETVAL);
};
XS_INTERNAL(Affix_Lib_DESTROY) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "$lib");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    infix_library_t * lib = INT2PTR(infix_library_t *, tmp);
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
                    safefree(entry);
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
    infix_library_t * lib = infix_library_open(path);
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
    if (err.message[0] != '\0')
        ST(0) = sv_2mortal(newSVpv(err.message, 0));
#if defined(INFIX_OS_WINDOWS)
    else if (err.system_error_code != 0) {
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr,
                       err.system_error_code,
                       0,
                       buf,
                       sizeof(buf),
                       nullptr);
        ST(0) = sv_2mortal(newSVpvf("System error: %s (code %ld)", buf, err.system_error_code));
    }
#endif
    else
        ST(0) = sv_2mortal(newSVpvf("Infix error code %d at position %zu", (int)err.code, err.position));
    XSRETURN(1);
}
static MGVTBL Affix_pin_vtbl = {
    Affix_get_pin, Affix_set_pin, Affix_len_pin, nullptr, Affix_free_pin, nullptr, nullptr, nullptr};
Affix_Pin * _get_pin_from_sv(pTHX_ SV * sv) {
    //~ warn("**************** Getting pin get_from_sv");

    if (!sv || !is_pin(aTHX_ sv))
        return nullptr;
    MAGIC * mg = mg_findext(SvRV(sv), PERL_MAGIC_ext, &Affix_pin_vtbl);
    if (mg)
        return (Affix_Pin *)mg->mg_ptr;
    return nullptr;
}

int Affix_set_pin(pTHX_ SV * sv, MAGIC * mg) {
    //~ warn("**************** Getting pin set");

    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type)
        return 0;
    sv2ptr(aTHX_ sv, pin->pointer, pin->type);
    return 0;
}

U32 Affix_len_pin(pTHX_ SV * sv, MAGIC * mg) {
    //~ warn("**************** Getting pin len");
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->pointer || !pin->type) {
        if (SvTYPE(sv) == SVt_PVAV)
            return av_len(MUTABLE_AV(sv));
        return sv_len(sv);
    }
    return pin->type->size;
}

int Affix_free_pin(pTHX_ SV * sv, MAGIC * mg) {
    //~ warn("**************** Getting pin free");

    PERL_UNUSED_VAR(sv);
    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (pin == nullptr)
        return 0;
    if (pin->managed && pin->pointer)
        safefree(pin->pointer);
    if (pin->type_arena != nullptr)
        infix_arena_destroy(pin->type_arena);
    safefree(pin);
    mg->mg_ptr = nullptr;
    return 0;
}
int Affix_get_pin(pTHX_ SV * sv, MAGIC * mg) {
    //~ warn("**************** Getting pin get");

    Affix_Pin * pin = (Affix_Pin *)mg->mg_ptr;
    if (!pin || !pin->type) {
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }
    if (pin->type->category == INFIX_TYPE_PRIMITIVE &&
        (pin->type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         pin->type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
        if (pin->pointer == NULL) {
            sv_setsv_mg(sv, &PL_sv_undef);
        }
        else {
            SV * new_sv = newSVpv((const char *)pin->pointer, 0);
            sv_setsv_mg(sv, new_sv);
            SvREFCNT_dec(new_sv);
        }
        return 0;
    }
    if (!pin->pointer) {
        sv_setsv_mg(sv, &PL_sv_undef);
        return 0;
    }
    ptr2sv(aTHX_ pin->pointer, sv, pin->type);
    return 0;
}

bool is_pin(pTHX_ SV * sv) {
    //~ warn("**************** Getting pin is");

    if (!sv || !SvOK(sv) || !SvROK(sv) || !SvMAGICAL(SvRV(sv)))
        return false;
    return mg_findext(SvRV(sv), PERL_MAGIC_ext, &Affix_pin_vtbl) != nullptr;
}
void _pin_sv(pTHX_ SV * sv, const infix_type * type, void * pointer, bool managed) {
    if (SvREADONLY(sv))
        return;
    SvUPGRADE(sv, SVt_PVMG);
    MAGIC * mg = mg_findext(sv, PERL_MAGIC_ext, &Affix_pin_vtbl);
    Affix_Pin * pin;
    if (mg) {
        pin = (Affix_Pin *)mg->mg_ptr;
        if (pin && pin->managed && pin->pointer)
            safefree(pin->pointer);
        if (pin && pin->type_arena) {
            infix_arena_destroy(pin->type_arena);
            pin->type_arena = nullptr;
        }
    }
    else {
        Newxz(pin, 1, Affix_Pin);
        mg = sv_magicext(sv, nullptr, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
    }
    pin->pointer = pointer;
    pin->type = type;
    pin->managed = managed;
    pin->type_arena = infix_arena_create(2048);
    if (!pin->type_arena) {
        safefree(pin);
        mg->mg_ptr = nullptr;
        croak("Failed to create memory arena for pin's type information");
    }
    pin->type = _copy_type_graph_to_arena(pin->type_arena, type);
    if (!pin->type) {
        infix_arena_destroy(pin->type_arena);
        safefree(pin);
        mg->mg_ptr = nullptr;
        croak("Failed to copy type information into pin");
    }
}
XS_INTERNAL(Affix_find_symbol) {
    dXSARGS;
    if (items != 2 || !sv_isobject(ST(0)) || !sv_derived_from(ST(0), "Affix::Lib"))
        croak_xs_usage(cv, "Affix_Lib_object, symbol_name");
    IV tmp = SvIV((SV *)SvRV(ST(0)));
    infix_library_t * lib = INT2PTR(infix_library_t *, tmp);
    const char * name = SvPV_nolen(ST(1));
    void * symbol = infix_library_get_symbol(lib, name);
    if (symbol) {
        Affix_Pin * pin;
        Newxz(pin, 1, Affix_Pin);
        pin->pointer = symbol;
        pin->managed = false;
        pin->type_arena = infix_arena_create(256);
        infix_type * void_ptr_type = nullptr;
        if (infix_type_create_pointer_to(pin->type_arena, &void_ptr_type, infix_type_create_void()) != INFIX_SUCCESS) {
            safefree(pin);
            infix_arena_destroy(pin->type_arena);
            croak("Internal error: Failed to create pointer type for pin");
        }
        pin->type = void_ptr_type;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(pin));
        SV * rv = newRV_inc(obj_data);
        //~ sv_bless(rv, gv_stashpv("Affix::Pointer", GV_ADD));
        sv_magicext(obj_data, nullptr, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
        ST(0) = sv_2mortal(rv);
        XSRETURN(1);
    }
    XSRETURN_UNDEF;
}
XS_INTERNAL(Affix_pin) {
    dXSARGS;
    dMY_CXT;
    if (items != 4)
        croak_xs_usage(cv, "var, lib, symbol, type");
    SV * target_sv = ST(0);
    const char * lib_path_or_name = SvPV_nolen(ST(1));
    const char * symbol_name = SvPV_nolen(ST(2));
    const char * signature = SvPV_nolen(ST(3));
    infix_library_t * lib = infix_library_open(lib_path_or_name);
    if (lib == nullptr) {
        croak(
            "Failed to load library from path '%s' for pinning: %s", lib_path_or_name, infix_get_last_error().message);
    }
    void * ptr = infix_library_get_symbol(lib, symbol_name);
    infix_library_close(lib);
    if (ptr == nullptr)
        croak("Failed to locate symbol '%s' in library '%s'", symbol_name, lib_path_or_name);
    infix_type * type = nullptr;
    infix_arena_t * arena = nullptr;
    if (infix_type_from_signature(&type, &arena, signature, MY_CXT.registry) != INFIX_SUCCESS)
        croak_sv(_format_parse_error(aTHX_ "for pin", signature, infix_get_last_error()));
    _pin_sv(aTHX_ target_sv, type, ptr, false);
    infix_arena_destroy(arena);
    XSRETURN_YES;
}
XS_INTERNAL(Affix_unpin) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "var");
    if (mg_findext(ST(0), PERL_MAGIC_ext, &Affix_pin_vtbl) && !sv_unmagicext(ST(0), PERL_MAGIC_ext, &Affix_pin_vtbl))
        XSRETURN_YES;
    XSRETURN_NO;
}
XS_INTERNAL(Affix_sizeof) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "type_signature");
    const char * signature = SvPV_nolen(ST(0));
    infix_type * type = nullptr;
    infix_arena_t * arena = nullptr;
    if (infix_type_from_signature(&type, &arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (arena)
            infix_arena_destroy(arena);
        croak_sv(_format_parse_error(aTHX_ "for sizeof", signature, infix_get_last_error()));
    }
    size_t type_size = infix_type_get_size(type);
    infix_arena_destroy(arena);
    ST(0) = sv_2mortal(newSVuv(type_size));
    XSRETURN(1);
}
void push_sint8(pTHX_ const infix_type * t, SV * sv, void * p) { *(int8_t *)p = SvIOK(sv) ? (int8_t)SvIV(sv) : 0; }
void push_uint8(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint8_t *)p = (SvUOK(sv) || SvIOK(sv)) ? (uint8_t)SvUV(sv) : 0;
}
void push_sint16(pTHX_ const infix_type * t, SV * sv, void * p) { *(int16_t *)p = SvIOK(sv) ? (int16_t)SvIV(sv) : 0; }
void push_uint16(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint16_t *)p = (SvUOK(sv) || SvIOK(sv)) ? (uint16_t)SvUV(sv) : 0;
}
void push_sint32(pTHX_ const infix_type * t, SV * sv, void * p) { *(int32_t *)p = SvIOK(sv) ? (int32_t)SvIV(sv) : 0; }
void push_uint32(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint32_t *)p = (SvUOK(sv) || SvIOK(sv)) ? (uint32_t)SvUV(sv) : 0;
}
void push_sint64(pTHX_ const infix_type * t, SV * sv, void * p) { *(int64_t *)p = SvIOK(sv) ? (int64_t)SvIV(sv) : 0; }
void push_uint64(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(uint64_t *)p = (SvUOK(sv) || SvIOK(sv)) ? (uint64_t)SvUV(sv) : 0;
}
void push_float(pTHX_ const infix_type * t, SV * sv, void * p) { *(float *)p = SvNOK(sv) ? (float)SvNV(sv) : 0; }
void push_double(pTHX_ const infix_type * t, SV * sv, void * p) { *(double *)p = SvNOK(sv) ? (double)SvNV(sv) : 0; }
void push_long_double(pTHX_ const infix_type * t, SV * sv, void * p) {
    *(long double *)p = SvNOK(sv) ? (long double)SvNV(sv) : 0;
}
void push_bool(pTHX_ const infix_type * t, SV * sv, void * p) { *(bool *)p = SvTRUE(sv); }
#if !defined(INFIX_COMPILER_MSVC)
void push_sint128(pTHX_ const infix_type * t, SV * sv, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
void push_uint128(pTHX_ const infix_type * t, SV * sv, void * p) {
    croak("128-bit integer marshalling not yet implemented");
}
#endif
void push_struct(pTHX_ const infix_type * type, SV * sv, void * p) {
    HV * hv;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVHV)
        hv = (HV *)SvRV(sv);
    else if (SvTYPE(sv) == SVt_PVHV)
        hv = (HV *)sv;
    else
        croak("Expected a HASH or HASH reference for struct marshalling");
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (!member->name)
            continue;
        void * member_ptr = (char *)p + member->offset;
        SV ** member_sv_ptr = hv_fetch(hv, member->name, strlen(member->name), 0);
        if (member_sv_ptr)
            sv2ptr(aTHX_ * member_sv_ptr, member_ptr, member->type);
    }
}
void push_union(pTHX_ const infix_type * type, SV * sv, void * p) {
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVHV)
        croak("Expected a HASH reference for union marshalling");
    HV * hv = (HV *)SvRV(sv);
    if (hv_iterinit(hv) == 0)
        return;
    HE * he = hv_iternext(hv);
    const char * key = HeKEY(he);
    STRLEN key_len = HeKLEN(he);
    SV * value_sv = HeVAL(he);
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (member->name && strlen(member->name) == key_len && memcmp(member->name, key, key_len) == 0) {
            sv2ptr(aTHX_ value_sv, p, member->type);
            return;
        }
    }
    croak("Union member '%s' not found in type definition", key);
}
void push_array(pTHX_ const infix_type * type, SV * sv, void * p) {
    const infix_type * element_type = type->meta.array_info.element_type;
    size_t c_array_len = type->meta.array_info.num_elements;
    if (element_type->category == INFIX_TYPE_PRIMITIVE &&
        (element_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         element_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8) &&
        SvPOK(sv)) {
        STRLEN perl_len;
        const char * perl_str = SvPV(sv, perl_len);
        if (perl_len >= c_array_len) {
            memcpy(p, perl_str, c_array_len - 1);
            ((char *)p)[c_array_len - 1] = '\0';
        }
        else {
            memcpy(p, perl_str, perl_len + 1);
        }
        return;
    }
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference for array marshalling");
    AV * av = (AV *)SvRV(sv);
    size_t perl_array_len = av_len(av) + 1;
    size_t num_to_copy = perl_array_len < c_array_len ? perl_array_len : c_array_len;
    if (perl_array_len > c_array_len)
        warn("Perl array has more elements (%lu) than C array capacity (%lu). Truncating.",
             (unsigned long)perl_array_len,
             (unsigned long)c_array_len);
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_to_copy; ++i) {
        SV ** element_sv_ptr = av_fetch(av, i, 0);
        if (element_sv_ptr) {
            void * element_ptr = (char *)p + (i * element_size);
            sv2ptr(aTHX_ * element_sv_ptr, element_ptr, element_type);
        }
    }
}
void push_enum(pTHX_ const infix_type * type, SV * sv, void * p) {
    sv2ptr(aTHX_ sv, p, type->meta.enum_info.underlying_type);
}
void push_complex(pTHX_ const infix_type * type, SV * sv, void * p) {
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference with two numbers for complex type marshalling");
    AV * av = (AV *)SvRV(sv);
    if (av_len(av) != 1)
        croak("Expected exactly two elements (real, imaginary) for complex type");
    const infix_type * base_type = type->meta.complex_info.base_type;
    size_t base_size = infix_type_get_size(base_type);
    SV ** real_sv_ptr = av_fetch(av, 0, 0);
    SV ** imag_sv_ptr = av_fetch(av, 1, 0);
    if (!real_sv_ptr || !imag_sv_ptr)
        croak("Failed to fetch real or imaginary part from array for complex type");
    sv2ptr(aTHX_ * real_sv_ptr, p, base_type);
    sv2ptr(aTHX_ * imag_sv_ptr, (char *)p + base_size, base_type);
}
void push_vector(pTHX_ const infix_type * type, SV * sv, void * p) {
    if (!SvROK(sv) || SvTYPE(SvRV(sv)) != SVt_PVAV)
        croak("Expected an ARRAY reference for vector marshalling");
    AV * av = (AV *)SvRV(sv);
    size_t num_elements = av_len(av) + 1;
    size_t c_vector_len = type->meta.vector_info.num_elements;
    if (num_elements != c_vector_len)
        croak("Perl array has %lu elements, but C vector type requires %lu.",
              (unsigned long)num_elements,
              (unsigned long)c_vector_len);
    const infix_type * element_type = type->meta.vector_info.element_type;
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_elements; ++i) {
        SV ** element_sv_ptr = av_fetch(av, i, 0);
        if (element_sv_ptr) {
            void * element_ptr = (char *)p + (i * element_size);
            sv2ptr(aTHX_ * element_sv_ptr, element_ptr, element_type);
        }
    }
}
void push_pointer(pTHX_ const infix_type * type, SV * sv, void * ptr) {
    if (is_pin(aTHX_ sv)) {
        *(void **)ptr = _get_pin_from_sv(aTHX_ sv)->pointer;
        return;
    }
    const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
    if (pointee_type == NULL)
        croak("Internal error in push_pointer: pointee_type is NULL");
    if (!SvOK(sv)) {
        *(void **)ptr = NULL;
        return;
    }
    if (SvROK(sv)) {
        SV * const rv = SvRV(sv);
        if (SvTYPE(rv) == SVt_PVAV) {
            AV * av = (AV *)rv;
            size_t len = av_len(av) + 1;
            size_t element_size = infix_type_get_size(pointee_type);
            char * c_array = (char *)safecalloc(len, element_size);
            for (size_t i = 0; i < len; ++i) {
                SV ** elem_sv_ptr = av_fetch(av, i, 0);
                if (elem_sv_ptr)
                    sv2ptr(aTHX_ * elem_sv_ptr, c_array + (i * element_size), pointee_type);
            }
            _pin_sv(aTHX_ sv, type, c_array, true);
            *(void **)ptr = c_array;
            return;
        }
        if (SvTYPE(rv) == SVt_PVCV) {
            if (pointee_type->category == INFIX_TYPE_REVERSE_TRAMPOLINE) {
                push_reverse_trampoline(aTHX_ pointee_type, sv, ptr);
                return;
            }
        }
        const infix_type * copy_type = (pointee_type->category == INFIX_TYPE_VOID)
            ? (SvIOK(rv)       ? infix_type_create_primitive(INFIX_PRIMITIVE_SINT64)
                   : SvNOK(rv) ? infix_type_create_primitive(INFIX_PRIMITIVE_DOUBLE)
                   : SvPOK(rv) ? (*(void **)ptr = SvPV_nolen(rv), (infix_type *)NULL)
                               : (croak("Cannot pass reference to this type of scalar for a 'void*' parameter"),
                                  (infix_type *)NULL))
            : pointee_type;
        if (!copy_type)
            return;
        void * dest_c_ptr = safecalloc(1, infix_type_get_size(copy_type));
        SV * sv_to_marshal = (SvTYPE(rv) == SVt_PVHV) ? sv : rv;
        sv2ptr(aTHX_ sv_to_marshal, dest_c_ptr, copy_type);
        _pin_sv(aTHX_ sv, copy_type, dest_c_ptr, true);
        *(void **)ptr = dest_c_ptr;
        return;
    }
    if (SvPOK(sv) && pointee_type->category == INFIX_TYPE_PRIMITIVE &&
        (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
        *(const char **)ptr = SvPV_nolen(sv);
        return;
    }
    croak("Don't know how to handle this type of scalar as a pointer argument");
}
void push_sv(pTHX_ const infix_type * type, SV * sv, void * ptr) {
    SvREFCNT_inc(sv);
    *(void **)ptr = sv;
}
SV * pull_sint8(pTHX_ const infix_type * t, void * p) { return newSViv(*(int8_t *)p); }
SV * pull_uint8(pTHX_ const infix_type * t, void * p) { return newSVuv(*(uint8_t *)p); }
SV * pull_sint16(pTHX_ const infix_type * t, void * p) { return newSViv(*(int16_t *)p); }
SV * pull_uint16(pTHX_ const infix_type * t, void * p) { return newSVuv(*(uint16_t *)p); }
SV * pull_sint32(pTHX_ const infix_type * t, void * p) { return newSViv(*(int32_t *)p); }
SV * pull_uint32(pTHX_ const infix_type * t, void * p) { return newSVuv(*(uint32_t *)p); }
SV * pull_sint64(pTHX_ const infix_type * t, void * p) { return newSViv(*(int64_t *)p); }
SV * pull_uint64(pTHX_ const infix_type * t, void * p) { return newSVuv(*(uint64_t *)p); }
SV * pull_float(pTHX_ const infix_type * t, void * p) { return newSVnv(*(float *)p); }
SV * pull_double(pTHX_ const infix_type * t, void * p) { return newSVnv(*(double *)p); }
SV * pull_long_double(pTHX_ const infix_type * t, void * p) { return newSVnv(*(long double *)p); }
SV * pull_bool(pTHX_ const infix_type * t, void * p) { return newSVbool(*(bool *)p); }
SV * pull_void(pTHX_ const infix_type * t, void * p) { return newSV(0); }
#if !defined(INFIX_COMPILER_MSVC)
SV * pull_sint128(pTHX_ const infix_type * t, void * p) { croak("128-bit integer marshalling not yet implemented"); }
SV * pull_uint128(pTHX_ const infix_type * t, void * p) { croak("128-bit integer marshalling not yet implemented"); }
#endif
SV * pull_struct(pTHX_ const infix_type * type, void * p) {
    HV * hv = newHV();
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (member->name) {
            void * member_ptr = (char *)p + member->offset;
            SV * member_sv = newSV(0);
            ptr2sv(aTHX_ member_ptr, member_sv, member->type);
            hv_store(hv, member->name, strlen(member->name), member_sv, 0);
        }
    }
    return newRV_inc(MUTABLE_SV(hv));
}
SV * pull_union(pTHX_ const infix_type * type, void * p) {
    croak("Cannot pull a C union directly; the active member is unknown.");
    return newSV(0);
}
SV * pull_array(pTHX_ const infix_type * type, void * p) {
    const infix_type * element_type = type->meta.array_info.element_type;
    if (element_type->category == INFIX_TYPE_PRIMITIVE &&
        (element_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
         element_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
        return newSVpv((const char *)p, 0);
    }
    AV * av = newAV();
    size_t num_elements = type->meta.array_info.num_elements;
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_elements; ++i) {
        void * element_ptr = (char *)p + (i * element_size);
        SV * element_sv = newSV(0);
        ptr2sv(aTHX_ element_ptr, element_sv, element_type);
        av_push(av, element_sv);
    }
    return newRV_inc(MUTABLE_SV(av));
}
SV * pull_reverse_trampoline(pTHX_ const infix_type * type, void * p) {
    void * func_ptr = *(void **)p;
    return newSViv(PTR2IV(func_ptr));
}
SV * pull_enum(pTHX_ const infix_type * type, void * p) {
    SV * sv = newSV(0);
    ptr2sv(aTHX_ p, sv, type->meta.enum_info.underlying_type);
    return sv;
}
SV * pull_complex(pTHX_ const infix_type * type, void * p) {
    AV * av = newAV();
    const infix_type * base_type = type->meta.complex_info.base_type;
    size_t base_size = infix_type_get_size(base_type);
    void * real_ptr = p;
    void * imag_ptr = (char *)p + base_size;
    SV * real_sv = newSV(0);
    ptr2sv(aTHX_ real_ptr, real_sv, base_type);
    av_push(av, real_sv);
    SV * imag_sv = newSV(0);
    ptr2sv(aTHX_ imag_ptr, imag_sv, base_type);
    av_push(av, imag_sv);
    return newRV_inc(MUTABLE_SV(av));
}
SV * pull_vector(pTHX_ const infix_type * type, void * p) {
    AV * av = newAV();
    const infix_type * element_type = type->meta.vector_info.element_type;
    size_t num_elements = type->meta.vector_info.num_elements;
    size_t element_size = infix_type_get_size(element_type);
    for (size_t i = 0; i < num_elements; ++i) {
        void * element_ptr = (char *)p + (i * element_size);
        SV * element_sv = newSV(0);
        ptr2sv(aTHX_ element_ptr, element_sv, element_type);
        av_push(av, element_sv);
    }
    return newRV_inc(MUTABLE_SV(av));
}
SV * pull_pointer(pTHX_ const infix_type * type, void * ptr) {
    void * c_ptr = *(void **)ptr;
    const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
    const char * name = infix_type_get_name(type);
    if (name && strcmp(name, "HeapString") == 0) {
        Affix_Pin * pin;
        Newxz(pin, 1, Affix_Pin);
        pin->pointer = c_ptr;
        pin->type = pointee_type;
        pin->managed = false;
        pin->type_arena = NULL;
        SV * obj_data = newSV(0);
        sv_setiv(obj_data, PTR2IV(pin));
        SV * rv = newRV_noinc(obj_data);
        //~ sv_bless(rv, gv_stashpv("Affix::Pointer", GV_ADD));
        sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
        return rv;
    }
    if (c_ptr == NULL)
        return newSV(0);
    switch (pointee_type->category) {
    case INFIX_TYPE_STRUCT:
        return pull_struct(aTHX_ pointee_type, c_ptr);
    case INFIX_TYPE_ARRAY:
        return pull_array(aTHX_ pointee_type, c_ptr);
    case INFIX_TYPE_PRIMITIVE:
        if (pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
            pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8) {
            return newSVpv((const char *)c_ptr, 0);
        }
    }
    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);
    pin->pointer = c_ptr;
    pin->type = pointee_type;
    pin->managed = false;
    pin->type_arena = NULL;
    SV * obj_data = newSV(0);
    sv_setiv(obj_data, PTR2IV(pin));
    SV * rv = newRV_noinc(obj_data);
    //~ sv_bless(rv, gv_stashpv("Affix::Pointer", GV_ADD));
    sv_magicext(obj_data, NULL, PERL_MAGIC_ext, &Affix_pin_vtbl, (const char *)pin, 0);
    return rv;
}
SV * pull_sv(pTHX_ const infix_type * type, void * ptr) {
    void * c_ptr = *(void **)ptr;
    if (c_ptr == NULL)
        return newSV(0);
    return SvREFCNT_inc((MUTABLE_SV(c_ptr)));
}
static const Affix_Push push_handlers[] = {[INFIX_PRIMITIVE_BOOL] = push_bool,
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
                                           [INFIX_PRIMITIVE_UINT128] = push_uint128
#endif
};
static const Affix_Pull pull_handlers[] = {[INFIX_PRIMITIVE_BOOL] = pull_bool,
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
                                           [INFIX_PRIMITIVE_UINT128] = pull_uint128
#endif
};
Affix_Push get_push_handler(const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        return push_handlers[type->meta.primitive_id];
    case INFIX_TYPE_POINTER:
        {
            const char * name = infix_type_get_name(type);
            if (name && strnEQ(name, "SV", 2))
                return push_sv;
            return push_pointer;
        }
    case INFIX_TYPE_STRUCT:
        return push_struct;
    case INFIX_TYPE_UNION:
        return push_union;
    case INFIX_TYPE_ARRAY:
        return push_array;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        return push_reverse_trampoline;
    case INFIX_TYPE_ENUM:
        return push_enum;
    case INFIX_TYPE_COMPLEX:
        return push_complex;
    case INFIX_TYPE_VECTOR:
        return push_vector;
    default:
        return nullptr;
    }
}
Affix_Pull get_pull_handler(const infix_type * type) {
    switch (type->category) {
    case INFIX_TYPE_PRIMITIVE:
        return pull_handlers[type->meta.primitive_id];
    case INFIX_TYPE_POINTER:
        {
            const char * name = infix_type_get_name(type);
            if (name && strnEQ(name, "SV", 2))
                return pull_sv;
            return pull_pointer;
        }
    case INFIX_TYPE_STRUCT:
        return pull_struct;
    case INFIX_TYPE_UNION:
        return pull_union;
    case INFIX_TYPE_ARRAY:
        return pull_array;
    case INFIX_TYPE_REVERSE_TRAMPOLINE:
        return pull_reverse_trampoline;
    case INFIX_TYPE_ENUM:
        return pull_enum;
    case INFIX_TYPE_COMPLEX:
        return pull_complex;
    case INFIX_TYPE_VECTOR:
        return pull_vector;
    case INFIX_TYPE_VOID:
        return pull_void;
    default:
        return nullptr;
    }
}
void ptr2sv(pTHX_ void * c_ptr, SV * perl_sv, const infix_type * type) {
    Affix_Pull h = get_pull_handler(type);
    if (!h) {
        char buffer[128];
        if (infix_type_print(buffer, sizeof(buffer), type, INFIX_DIALECT_SIGNATURE) == INFIX_SUCCESS)
            croak("Cannot convert C type to Perl SV. Unsupported type: %s", buffer);
        croak("Cannot convert C type to Perl SV. Unsupported type.");
    }
    SV * new_sv = h(aTHX_ type, c_ptr);
    if (new_sv != &PL_sv_undef) {
        sv_setsv_mg(perl_sv, new_sv);
        SvREFCNT_dec(new_sv);
    }
    else
        sv_setsv_mg(perl_sv, &PL_sv_undef);
}
void sv2ptr(pTHX_ SV * perl_sv, void * c_ptr, const infix_type * type) {
    Affix_Push h = get_push_handler(type);
    if (!h)
        croak("Cannot convert Perl SV to C type: unsupported type");
    h(aTHX_ type, perl_sv, c_ptr);
}
void _populate_hv_from_c_struct(pTHX_ HV * hv, const infix_type * type, void * p) {
    hv_clear(hv);
    for (size_t i = 0; i < type->meta.aggregate_info.num_members; ++i) {
        const infix_struct_member * member = &type->meta.aggregate_info.members[i];
        if (member->name) {
            void * member_ptr = (char *)p + member->offset;
            SV * member_sv = newSV(0);
            ptr2sv(aTHX_ member_ptr, member_sv, member->type);
            hv_store(hv, member->name, strlen(member->name), member_sv, 0);
        }
    }
}
void Affix_trigger(pTHX_ CV * cv) {
    dSP;
    dAXMARK;
    Affix * affix = (Affix *)CvXSUBANY(cv).any_ptr;
    if (!affix)
        croak("Internal error: Affix context is nullptr in trigger");
    size_t num_args = infix_forward_get_num_args(affix->infix);
    if ((SP - MARK) != num_args)
        croak("Wrong number of arguments to affixed function. Expected %" UVuf ", got %" UVuf,
              (UV)num_args,
              (UV)(SP - MARK));
    bool * is_pre_pinned;
    Newx(is_pre_pinned, num_args, bool);
    for (size_t i = 0; i < num_args; ++i)
        is_pre_pinned[i] = is_pin(aTHX_ ST(i));
    affix->args_arena->current_offset = 0;
    affix->ret_arena->current_offset = 0;
    void ** c_args = infix_arena_alloc(affix->args_arena, sizeof(void *) * num_args, _Alignof(void *));
    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * arg_type = infix_forward_get_arg_type(affix->infix, i);
        void * c_arg_ptr =
            infix_arena_alloc(affix->args_arena, infix_type_get_size(arg_type), infix_type_get_alignment(arg_type));
        affix->push[i](aTHX_ arg_type, ST(i), c_arg_ptr);
        c_args[i] = c_arg_ptr;
    }
    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    void * ret_ptr =
        infix_arena_alloc(affix->ret_arena, infix_type_get_size(ret_type), infix_type_get_alignment(ret_type));
    affix->cif(ret_ptr, c_args);
    if (affix->num_out_params > 0) {
        for (size_t i = 0; i < affix->num_out_params; ++i) {
            OutParamInfo info = affix->out_param_info[i];
            if (is_pre_pinned[info.index])
                continue;
            SV * arg_sv = ST(info.index);
            if (SvROK(arg_sv)) {
                SV * rsv = SvRV(arg_sv);
                if (SvTYPE(rsv) == SVt_PVAV && info.pointee_type->category == INFIX_TYPE_STRUCT)
                    continue;
                void * data_ptr = *(void **)c_args[info.index];
                if (info.pointee_type->category == INFIX_TYPE_POINTER) {
                    const infix_type * inner_pointee_type = info.pointee_type->meta.pointer_info.pointee_type;
                    if (inner_pointee_type->category == INFIX_TYPE_PRIMITIVE &&
                        (inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_SINT8 ||
                         inner_pointee_type->meta.primitive_id == INFIX_PRIMITIVE_UINT8)) {
                        sv_setpv(rsv, *(char **)data_ptr);
                    }
                    else if (SvROK(rsv)) {
                        ptr2sv(aTHX_ * (void **)data_ptr, SvRV(rsv), inner_pointee_type);
                    }
                }
                else if (info.pointee_type->category == INFIX_TYPE_STRUCT && SvTYPE(rsv) == SVt_PVHV) {
                    _populate_hv_from_c_struct(aTHX_(HV *) rsv, info.pointee_type, data_ptr);
                }
                else {
                    ptr2sv(aTHX_ data_ptr, rsv, info.pointee_type);
                }
            }
        }
    }
    Safefree(is_pre_pinned);
    SV * return_sv = affix->pull(aTHX_ ret_type, ret_ptr);
    ST(0) = sv_2mortal(return_sv);
    XSRETURN(1);
}
static infix_library_t * _get_lib_from_registry(pTHX_ const char * path) {
    dMY_CXT;
    const char * lookup_path = (path == NULL) ? "" : path;

    SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
    if (entry_sv_ptr) {
        LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
        entry->ref_count++;
        return entry->lib;
    }

    infix_library_t * lib = infix_library_open(path);
    if (lib) {
        LibRegistryEntry * new_entry;
        Newxz(new_entry, 1, LibRegistryEntry);
        new_entry->lib = lib;
        new_entry->ref_count = 1;
        hv_store(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), newSViv(PTR2IV(new_entry)), 0);
        return lib;
    }
    return NULL;
}
XS_INTERNAL(Affix_affix) {
    dXSARGS;
    dXSI32;
    dMY_CXT;
    if (items != 3)
        croak_xs_usage(cv, "Affix::affix($target, $name_spec, $signature)");

    void * symbol = nullptr;
    char * rename = nullptr;
    infix_library_t * lib_handle_for_symbol = nullptr;
    bool created_implicit_handle = false;

    SV * target_sv = ST(0);
    SV * name_sv = ST(1);
    const char * symbol_name_str = nullptr;
    const char * rename_str = nullptr;

    if (SvROK(name_sv) && SvTYPE(SvRV(name_sv)) == SVt_PVAV) {
        AV * name_av = (AV *)SvRV(name_sv);
        if (av_count(name_av) != 2)
            croak("Name spec arrayref must contain exactly two elements: [symbol_name, new_sub_name]");
        symbol_name_str = SvPV_nolen(*av_fetch(name_av, 0, 0));
        rename_str = SvPV_nolen(*av_fetch(name_av, 1, 0));
    }
    else {
        symbol_name_str = rename_str = SvPV_nolen(name_sv);
    }
    rename = (char *)rename_str;

    if (sv_isobject(target_sv) && sv_derived_from(target_sv, "Affix::Lib")) {
        IV tmp = SvIV((SV *)SvRV(target_sv));
        lib_handle_for_symbol = INT2PTR(infix_library_t *, tmp);
        created_implicit_handle = false;
    }
    else if (_get_pin_from_sv(aTHX_ target_sv)) {
        symbol = _get_pin_from_sv(aTHX_ target_sv)->pointer;
    }
    else {
        const char * path = SvOK(target_sv) ? SvPV_nolen(target_sv) : NULL;
        lib_handle_for_symbol = _get_lib_from_registry(aTHX_ path);
        if (lib_handle_for_symbol)
            created_implicit_handle = true;
    }

    if (lib_handle_for_symbol && !symbol)
        symbol = infix_library_get_symbol(lib_handle_for_symbol, symbol_name_str);

    if (symbol == nullptr) {
        if (created_implicit_handle) {
            const char * lookup_path = SvOK(target_sv) ? SvPV_nolen(target_sv) : "";
            SV ** entry_sv_ptr = hv_fetch(MY_CXT.lib_registry, lookup_path, strlen(lookup_path), 0);
            if (entry_sv_ptr) {
                LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(*entry_sv_ptr));
                entry->ref_count--;
                if (entry->ref_count == 0) {
                    infix_library_close(entry->lib);
                    safefree(entry);
                    hv_delete_ent(MY_CXT.lib_registry, newSVpvn(lookup_path, strlen(lookup_path)), G_DISCARD, 0);
                }
            }
        }
        XSRETURN_UNDEF;
    }

    Affix * affix;
    Newxz(affix, 1, Affix);

    if (created_implicit_handle)
        affix->lib_handle = lib_handle_for_symbol;
    else
        affix->lib_handle = NULL;

    const char * signature = SvPV_nolen(ST(2));
    infix_status status = infix_forward_create(&affix->infix, signature, symbol, MY_CXT.registry);
    if (status != INFIX_SUCCESS) {
        safefree(affix);
        croak_sv(_format_parse_error(aTHX_ "to create trampoline", signature, infix_get_last_error()));
    }
    affix->cif = infix_forward_get_code(affix->infix);
    size_t num_args = infix_forward_get_num_args(affix->infix);
    Newxz(affix->push, num_args, Affix_Push);
    affix->args_arena = infix_arena_create(4096);
    affix->ret_arena = infix_arena_create(1024);
    if (!affix->args_arena || !affix->ret_arena)
        croak("Failed to create memory arenas for FFI call");
    size_t out_param_count = 0;
    OutParamInfo * temp_out_info = safemalloc(sizeof(OutParamInfo) * num_args);
    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * type = infix_forward_get_arg_type(affix->infix, i);
        affix->push[i] = get_push_handler(type);
        if (affix->push[i] == nullptr) {
            safefree(temp_out_info);
            croak("Unsupported argument type in signature at index %zu", i);
        }

        if (type->category == INFIX_TYPE_POINTER) {
            const infix_type * pointee_type = type->meta.pointer_info.pointee_type;
            if (pointee_type->category == INFIX_TYPE_PRIMITIVE || pointee_type->category == INFIX_TYPE_STRUCT ||
                pointee_type->category == INFIX_TYPE_POINTER) {
                temp_out_info[out_param_count].index = i;
                temp_out_info[out_param_count].pointee_type = pointee_type;
                out_param_count++;
            }
        }
    }
    affix->num_out_params = out_param_count;
    if (out_param_count > 0) {
        affix->out_param_info = safemalloc(sizeof(OutParamInfo) * out_param_count);
        memcpy(affix->out_param_info, temp_out_info, sizeof(OutParamInfo) * out_param_count);
    }
    safefree(temp_out_info);
    const infix_type * ret_type = infix_forward_get_return_type(affix->infix);
    affix->pull = get_pull_handler(ret_type);
    if (affix->pull == nullptr)
        croak("Unsupported return type in signature");
    char prototype_buf[256] = {0};
    for (size_t i = 0; i < num_args; ++i)
        strcat(prototype_buf, "$");
    CV * cv_new = newXSproto_portable(ix == 0 ? rename : nullptr, Affix_trigger, __FILE__, prototype_buf);
    if (UNLIKELY(cv_new == nullptr))
        croak("Failed to install new XSUB");
    CvXSUBANY(cv_new).any_ptr = (void *)affix;
    SV * obj = newRV_inc(MUTABLE_SV(cv_new));
    sv_bless(obj, gv_stashpv("Affix", GV_ADD));
    ST(0) = sv_2mortal(obj);
    XSRETURN(1);
}
XS_INTERNAL(Affix_DESTROY) {
    dXSARGS;
    dMY_CXT;
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
    if (affix != nullptr) {
        if (affix->lib_handle != nullptr && MY_CXT.lib_registry != NULL) {
            hv_iterinit(MY_CXT.lib_registry);
            HE * he;
            while ((he = hv_iternext(MY_CXT.lib_registry))) {
                SV * entry_sv = HeVAL(he);
                LibRegistryEntry * entry = INT2PTR(LibRegistryEntry *, SvIV(entry_sv));
                if (entry->lib == affix->lib_handle) {
                    entry->ref_count--;
                    if (entry->ref_count == 0) {
                        infix_library_close(entry->lib);
                        safefree(entry);
                        hv_delete_ent(MY_CXT.lib_registry, HeKEY_sv(he), G_DISCARD, 0);
                    }
                    break;
                }
            }
        }

        if (affix->args_arena != nullptr)
            infix_arena_destroy(affix->args_arena);
        if (affix->ret_arena != nullptr)
            infix_arena_destroy(affix->ret_arena);
        if (affix->infix != nullptr)
            infix_forward_destroy(affix->infix);
        if (affix->push != nullptr)
            safefree(affix->push);
        if (affix->out_param_info != nullptr)
            safefree(affix->out_param_info);
        safefree(affix);
    }
    XSRETURN_EMPTY;
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
void _affix_callback_handler_entry(infix_context_t * ctx, void * retval, void ** args) {
    Affix_Callback_Data * cb_data = (Affix_Callback_Data *)infix_reverse_get_user_data(ctx);
    if (!cb_data)
        return;
    dTHXa(cb_data->perl);
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    size_t num_args = infix_reverse_get_num_args(ctx);
    for (size_t i = 0; i < num_args; ++i) {
        const infix_type * type = infix_reverse_get_arg_type(ctx, i);
        Affix_Pull puller = get_pull_handler(type);
        if (!puller)
            croak("Unsupported callback argument type");
        mXPUSHs(puller(aTHX_ type, args[i]));
    }
    PUTBACK;
    const infix_type * ret_type = infix_reverse_get_return_type(ctx);
    U32 call_flags = (ret_type->category == INFIX_TYPE_VOID) ? G_VOID : G_SCALAR;
    size_t count = call_sv(cb_data->coderef_rv, call_flags);
    SPAGAIN;

    if (call_flags == G_SCALAR) {
        if (count != 1)
            croak("Callback was expected to return 1 value, but returned %zu", count);
        SV * return_sv = POPs;
        Affix_Push pusher = get_push_handler(ret_type);
        if (pusher == nullptr)
            croak("Unsupported callback return type");
        pusher(aTHX_ ret_type, return_sv, retval);
    }
    PUTBACK;
    FREETMPS;
    LEAVE;
}
void push_reverse_trampoline(pTHX_ const infix_type * type, SV * sv, void * p) {
    dMY_CXT;
    if (SvROK(sv) && SvTYPE(SvRV(sv)) == SVt_PVCV) {
        SV * coderef_cv = SvRV(sv);

        char key[32];
        int key_len = snprintf(key, sizeof(key), "%p", (void *)coderef_cv);

        SV ** entry_sv_ptr = hv_fetch(MY_CXT.callback_registry, key, key_len, 0);

        if (entry_sv_ptr) {
            Implicit_Callback_Magic * magic_data = INT2PTR(Implicit_Callback_Magic *, SvIV(*entry_sv_ptr));
            *(void **)p = infix_reverse_get_code(magic_data->reverse_ctx);
        }
        else {
            Affix_Callback_Data * cb_data;
            Newxz(cb_data, 1, Affix_Callback_Data);
            cb_data->coderef_rv = newRV_inc(coderef_cv);
            storeTHX(cb_data->perl);

            infix_type * ret_type = type->meta.func_ptr_info.return_type;
            size_t num_args = type->meta.func_ptr_info.num_args;
            size_t num_fixed_args = type->meta.func_ptr_info.num_fixed_args;
            infix_type ** arg_types = NULL;

            if (num_args > 0) {
                Newx(arg_types, num_args, infix_type *);
                for (size_t i = 0; i < num_args; ++i)
                    arg_types[i] = type->meta.func_ptr_info.args[i].type;
            }

            infix_reverse_t * reverse_ctx = nullptr;
            infix_status status = infix_reverse_create_closure_manual(&reverse_ctx,
                                                                      ret_type,
                                                                      arg_types,
                                                                      num_args,
                                                                      num_fixed_args,
                                                                      (void *)_affix_callback_handler_entry,
                                                                      (void *)cb_data);
            if (arg_types)
                Safefree(arg_types);

            if (status != INFIX_SUCCESS) {
                SvREFCNT_dec(cb_data->coderef_rv);
                safefree(cb_data);
                char signature_buf[256];
                (void)infix_type_print(
                    signature_buf, sizeof(signature_buf), (infix_type *)type, INFIX_DIALECT_SIGNATURE);
                croak_sv(_format_parse_error(aTHX_ "for callback", signature_buf, infix_get_last_error()));
            }

            Implicit_Callback_Magic * magic_data;
            Newxz(magic_data, 1, Implicit_Callback_Magic);
            magic_data->reverse_ctx = reverse_ctx;

            hv_store(MY_CXT.callback_registry, key, key_len, newSViv(PTR2IV(magic_data)), 0);

            *(void **)p = infix_reverse_get_code(reverse_ctx);
        }
    }
    else if (!SvOK(sv) || !SvIV(sv)) {
        *(void **)p = NULL;
    }
    else {
        croak("Argument for a callback must be a code reference or undef.");
    }
}
XS_INTERNAL(Affix_as_string) {
    dVAR;
    dXSARGS;
    if (items < 1)
        croak_xs_usage(cv, "$affix");
    {
        char * RETVAL;
        dXSTARG;
        Affix * affix;
        if (sv_derived_from(ST(0), "Affix")) {
            IV tmp = SvIV((SV *)SvRV(ST(0)));
            affix = INT2PTR(Affix *, tmp);
        }
        else
            croak("affix is not of type Affix");
        RETVAL = (char *)affix->infix->target_fn;
        sv_setpv(TARG, RETVAL);
        XSprePUSH;
        PUSHTARG;
    }
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
#if DEBUG > 0
                if (entry->ref_count > 0) {
                    warn("Affix: library handle for '%s' has %d outstanding references at END.",
                         HeKEY(he),
                         (int)entry->ref_count);
                }
#endif
                if (entry->lib)
                    infix_library_close(entry->lib);
                safefree(entry);
            }
        }
        hv_undef(MY_CXT.lib_registry);
        MY_CXT.lib_registry = NULL;
    }
    if (MY_CXT.callback_registry) {
        hv_iterinit(MY_CXT.callback_registry);
        HE * he;
        while ((he = hv_iternext(MY_CXT.callback_registry))) {
            SV * entry_sv = HeVAL(he);
            Implicit_Callback_Magic * magic_data = INT2PTR(Implicit_Callback_Magic *, SvIV(entry_sv));
            if (magic_data) {
                infix_reverse_t * ctx = magic_data->reverse_ctx;
                if (ctx) {
                    Affix_Callback_Data * cb_data = (Affix_Callback_Data *)infix_reverse_get_user_data(ctx);
                    if (cb_data) {
                        SvREFCNT_dec(cb_data->coderef_rv);
                        safefree(cb_data);
                    }
                    infix_reverse_destroy(ctx);
                }
                safefree(magic_data);
            }
        }
        hv_undef(MY_CXT.callback_registry);
        MY_CXT.callback_registry = NULL;
    }
    if (MY_CXT.registry) {
        infix_registry_destroy(MY_CXT.registry);
        MY_CXT.registry = NULL;
    }
    XSRETURN_EMPTY;
}
XS_INTERNAL(Affix_typedef) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "types_string");
    const char * types = SvPV_nolen(ST(0));
    if (infix_register_types(MY_CXT.registry, types) != INFIX_SUCCESS)
        croak_sv(_format_parse_error(aTHX_ "in typedef", types, infix_get_last_error()));
    XSRETURN_YES;
}
void _DumpHex(pTHX_ const void * addr, size_t len, const char * file, int line) {
    if (addr == nullptr) {
        printf("Dumping %lu bytes from null pointer %p at %s line %d\n", (unsigned long)len, addr, file, line);
        fflush(stdout);
        return;
    }
    fflush(stdout);
    int perLine = 16;
    if (perLine < 4 || perLine > 64)
        perLine = 16;
    size_t i;
    U8 * buff;
    Newxz(buff, perLine + 1, U8);
    const U8 * pc = (const U8 *)addr;
    printf("Dumping %lu bytes from %p at %s line %d\n", (unsigned long)len, addr, file, line);
    if (len == 0)
        croak("ZERO LENGTH");
    for (i = 0; i < len; i++) {
        if ((i % perLine) == 0) {
            if (i != 0)
                printf(" | %s\n", buff);
            printf("#  %03zu ", i);
        }
        printf(" %02x", pc[i]);
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % perLine] = '.';
        else
            buff[i % perLine] = pc[i];
        buff[(i % perLine) + 1] = '\0';
    }
    while ((i % perLine) != 0) {
        printf("   ");
        i++;
    }
    printf(" | %s\n", buff);
    safefree(buff);
    fflush(stdout);
}
void _DD(pTHX_ SV * scalar, const char * file, int line) {
    Perl_load_module(aTHX_ PERL_LOADMOD_NOIMPORT, newSVpvs("Data::Printer"), nullptr, nullptr, nullptr);
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
XS_INTERNAL(Affix_sv_dump) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "sv");
    sv_dump(ST(0));
    XSRETURN_EMPTY;
}
static SV * _new_pointer_obj(pTHX_ Affix_Pin * pin) {
    SV * data_sv = newSV(0);
    SV * rv = newRV_inc(data_sv);
    //~ sv_bless(rv, gv_stashpv("Affix::Pointer", GV_ADD));
    sv_setiv(data_sv, PTR2IV(pin));
    _pin_sv(aTHX_ data_sv, pin->type, pin->pointer, true);
    return rv;
}
XS_INTERNAL(Affix_malloc) {
    dXSARGS;
    dMY_CXT;
    if (items != 1)
        croak_xs_usage(cv, "size");
    UV size = SvUV(ST(0));
    infix_type * type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&type, &parse_arena, "*void", MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for malloc", "*void", infix_get_last_error()));
    }
    if (size == 0) {
        infix_arena_destroy(parse_arena);
        croak("Cannot malloc a zero-sized type");
    }
    void * ptr = safemalloc(size);
    Affix_Pin * pin;
    Newx(pin, 1, Affix_Pin);
    pin->pointer = ptr;
    pin->managed = true;
    pin->type_arena = infix_arena_create(1024);
    pin->type = _copy_type_graph_to_arena(pin->type_arena, type);
    infix_arena_destroy(parse_arena);
    ST(0) = sv_2mortal(_new_pointer_obj(aTHX_ pin));
    XSRETURN(1);
}
XS_INTERNAL(Affix_calloc) {
    dXSARGS;
    dMY_CXT;
    if (items != 2)
        croak_xs_usage(cv, "count, type_signature");
    UV count = SvUV(ST(0));
    const char * signature = SvPV_nolen(ST(1));
    infix_type * elem_type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&elem_type, &parse_arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for calloc", signature, infix_get_last_error()));
    }
    size_t elem_size = infix_type_get_size(elem_type);
    if (elem_size == 0) {
        infix_arena_destroy(parse_arena);
        croak("Cannot calloc a zero-sized type");
    }
    void * ptr = safecalloc(count, elem_size);
    Affix_Pin * pin;
    Newxz(pin, 1, Affix_Pin);
    pin->pointer = ptr;
    pin->managed = true;
    pin->type_arena = infix_arena_create(1024);
    infix_type * array_type;
    if (infix_type_create_array(pin->type_arena, &array_type, elem_type, count) != INFIX_SUCCESS)
        croak("Failed to create array type graph.");
    pin->type = array_type;
    infix_arena_destroy(parse_arena);
    ST(0) = sv_2mortal(_new_pointer_obj(aTHX_ pin));
    XSRETURN(1);
}
XS_INTERNAL(Affix_realloc) {
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "self, new_size");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin || !pin->managed)
        croak("Can only realloc a managed pointer");
    UV new_size = SvUV(ST(1));
    void * new_ptr = saferealloc(pin->pointer, new_size);
    pin->pointer = new_ptr;
    XSRETURN_YES;
}
XS_INTERNAL(Affix_free) {
    dXSARGS;
    if (items != 1)
        croak_xs_usage(cv, "pointer_object");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin) {
        warn("Affix::free called on a non-pointer object");
        XSRETURN_NO;
    }
    if (!pin->managed)
        croak("Cannot free a pointer that was not allocated by Affix (it is unmanaged)");
    if (pin->pointer) {
        safefree(pin->pointer);
        pin->pointer = NULL;
    }
    XSRETURN_YES;
}
XS_INTERNAL(Affix_cast) {
    dXSARGS;
    dMY_CXT;
    if (items != 2)
        croak_xs_usage(cv, "self, new_type_signature");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin)
        croak("Argument is not a pointer");
    const char * signature = SvPV_nolen(ST(1));
    infix_type * new_type = NULL;
    infix_arena_t * parse_arena = NULL;
    if (infix_type_from_signature(&new_type, &parse_arena, signature, MY_CXT.registry) != INFIX_SUCCESS) {
        if (parse_arena)
            infix_arena_destroy(parse_arena);
        croak_sv(_format_parse_error(aTHX_ "for cast", signature, infix_get_last_error()));
    }
    if (pin->type_arena)
        infix_arena_destroy(pin->type_arena);
    pin->type_arena = infix_arena_create(1024);
    pin->type = _copy_type_graph_to_arena(pin->type_arena, new_type);
    infix_arena_destroy(parse_arena);
    ST(0) = ST(0);
    XSRETURN(1);
}
XS_INTERNAL(Affix_dump) {
    // TODO: Make this look for the pinned var's type and use sizeof calc from infix
    dVAR;
    dXSARGS;
    if (items != 2)
        croak_xs_usage(cv, "self, length_in_bytes");
    Affix_Pin * pin = _get_pin_from_sv(aTHX_ ST(0));
    if (!pin)
        croak("self is not a valid pointer");
    if (!pin->pointer) {
        warn("Cannot dump a NULL pointer");
        XSRETURN_EMPTY;
    }
    UV length = SvUV(ST(1));
    if (length == 0) {
        warn("Dump length cannot be zero");
        XSRETURN_EMPTY;
    }
    _DumpHex(aTHX_ pin->pointer, length, OutCopFILE(PL_curcop), CopLINE(PL_curcop));
    ST(0) = ST(0);
    XSRETURN(1);
}

void boot_Affix(pTHX_ CV * cv) {
    dXSBOOTARGSXSAPIVERCHK;
    PERL_UNUSED_VAR(items);
#ifdef USE_ITHREADS
    my_perl = (PerlInterpreter *)PERL_GET_CONTEXT;
#endif
    MY_CXT_INIT;
    MY_CXT.lib_registry = newHV();
    MY_CXT.callback_registry = newHV();
    MY_CXT.registry = infix_registry_create();
    if (!MY_CXT.registry)
        croak("Failed to initialize the global type registry");
    newXS("Affix::END", Affix_END, __FILE__);
    newXS("Affix::affix", Affix_affix, __FILE__);
    newXS("Affix::wrap", Affix_affix, __FILE__);
    newXS("Affix::DESTROY", Affix_DESTROY, __FILE__);
    sv_setsv(get_sv("Affix::()", TRUE), &PL_sv_yes);
    (void)newXSproto_portable("Affix::()", Affix_as_string, __FILE__, "$;@");
    newXS("Affix::load_library", Affix_load_library, __FILE__);
    sv_setsv(get_sv("Affix::Lib::()", TRUE), &PL_sv_yes);
    (void)newXSproto_portable("Affix::Lib::(0+", Affix_Lib_as_string, __FILE__, "$;@");
    (void)newXSproto_portable("Affix::Lib::()", Affix_as_string, __FILE__, "$;@");
    newXS("Affix::Lib::DESTROY", Affix_Lib_DESTROY, __FILE__);
    newXS("Affix::find_symbol", Affix_find_symbol, __FILE__);
    newXS("Affix::get_last_error_message", Affix_get_last_error_message, __FILE__);
    (void)newXSproto_portable("Affix::pin", Affix_pin, __FILE__, "$$$$");
    export_function("Affix", "pin", "pin");
    (void)newXSproto_portable("Affix::unpin", Affix_unpin, __FILE__, "$");
    export_function("Affix", "unpin", "pin");
    (void)newXSproto_portable("Affix::sizeof", Affix_sizeof, __FILE__, "$");
    (void)newXSproto_portable("Affix::typedef", Affix_typedef, __FILE__, "$");
    export_function("Affix", "sizeof", "core");
    export_function("Affix", "affix", "core");
    export_function("Affix", "wrap", "core");
    export_function("Affix", "load_library", "lib");
    export_function("Affix", "find_symbol", "lib");
    export_function("Affix", "get_last_error_message", "core");
    export_function("Affix", "typedef", "registry");
    (void)newXSproto_portable("Affix::sv_dump", Affix_sv_dump, __FILE__, "$");
    {
        (void)newXSproto_portable("Affix::malloc", Affix_malloc, __FILE__, "$");
        (void)newXSproto_portable("Affix::calloc", Affix_calloc, __FILE__, "$$");
        (void)newXSproto_portable("Affix::realloc", Affix_realloc, __FILE__, "$$");
        (void)newXSproto_portable("Affix::free", Affix_free, __FILE__, "$");
        (void)newXSproto_portable("Affix::cast", Affix_cast, __FILE__, "$$");
        (void)newXSproto_portable("Affix::dump", Affix_dump, __FILE__, "$$");
        export_function("Affix", "malloc", "mem");
        export_function("Affix", "calloc", "mem");
        export_function("Affix", "free", "mem");
    }
    Perl_xs_boot_epilog(aTHX_ ax);
}
